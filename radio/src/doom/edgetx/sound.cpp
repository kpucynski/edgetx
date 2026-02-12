/*
 * DOOM sound module for EdgeTX / TX-15
 *
 * Plays DOOM SFX through the TX-15 integrated speaker via TAS2505 / I2S.
 *
 * DOOM stores sound effects in the WAD as lumps prefixed with "DS".  The
 * lump format is:
 *   - uint16_t format   (must be 0x0003)
 *   - uint16_t sampleRate (typically 11025)
 *   - uint32_t numSamples
 *   - uint8_t  samples[]  (unsigned 8-bit PCM, with 16-byte pad on each end)
 *
 * We convert the 8-bit unsigned data to 16-bit signed, resample from the
 * original rate (usually 11025 Hz) to the EdgeTX I2S rate (32000 Hz) using
 * linear interpolation, mix up to NUM_CHANNELS simultaneous sounds, and push
 * the result into the EdgeTX AudioBufferFifo so the existing DMA ISR can
 * stream it to the TAS2505 codec.
 *
 * License: GPLv2
 */

#include "sound.h"

#include <string.h>
#include <stdlib.h>

extern "C" {
#include "w_wad.h"
#include "z_zone.h"
#include "doomtype.h"
}
#include "doomgeneric.h"

/* EdgeTX headers (C++) */
#include "audio.h"
#include "board.h"
#include "hal/audio_driver.h"

/* ------------------------------------------------------------------ */
/*  Constants                                                          */
/* ------------------------------------------------------------------ */

#define NUM_CHANNELS      8       /* simultaneous sound effects        */
#define DOOM_HEADER_SIZE  8       /* bytes before PCM data in a lump   */
#define DOOM_PAD_BYTES    16      /* padding bytes before/after samples */

/* Target sample rate (must match EdgeTX I2S) */
#define TARGET_RATE       AUDIO_SAMPLE_RATE   /* 32000 */

/* ------------------------------------------------------------------ */
/*  Per-channel state                                                  */
/* ------------------------------------------------------------------ */

typedef struct {
    const int16_t *data;        /* resampled 16-bit signed PCM          */
    uint32_t       length;      /* number of resampled samples          */
    uint32_t       pos;         /* current playback position            */
    int            vol;         /* 0-127 channel volume                 */
    int            sep;         /* 0-254 stereo separation (mono: 128)  */
    boolean        active;
} channel_t;

static channel_t channels[NUM_CHANNELS];

/* ------------------------------------------------------------------ */
/*  Sound lump cache                                                   */
/*                                                                     */
/*  We keep one resampled copy per unique lump so we don't resample    */
/*  the same effect every time it plays.                               */
/* ------------------------------------------------------------------ */

#define MAX_CACHED  64

typedef struct {
    int       lumpnum;
    int16_t  *data;          /* resampled buffer (malloc'd)            */
    uint32_t  length;        /* number of samples                      */
} cached_sound_t;

static cached_sound_t cache[MAX_CACHED];
static int cache_count = 0;

/* ------------------------------------------------------------------ */
/*  Helpers                                                            */
/* ------------------------------------------------------------------ */

/* Look up (or create) a resampled copy of a WAD sound lump. */
static cached_sound_t *get_cached_sound(int lumpnum)
{
    /* Already cached? */
    for (int i = 0; i < cache_count; i++) {
        if (cache[i].lumpnum == lumpnum)
            return &cache[i];
    }

    /* Load the raw lump */
    int lump_len = W_LumpLength(lumpnum);
    if (lump_len < (int)(DOOM_HEADER_SIZE + DOOM_PAD_BYTES * 2))
        return NULL;

    uint8_t *raw = (uint8_t *)W_CacheLumpNum(lumpnum, PU_STATIC);
    if (!raw) return NULL;

    /* Parse header */
    uint16_t fmt        = raw[0] | (raw[1] << 8);
    uint16_t src_rate   = raw[2] | (raw[3] << 8);
    uint32_t src_len    = raw[4] | (raw[5] << 8) | (raw[6] << 16) | (raw[7] << 24);

    if (fmt != 3 || src_rate == 0 || src_len < DOOM_PAD_BYTES * 2) {
        W_ReleaseLumpNum(lumpnum);
        return NULL;
    }

    /* Skip the 16-byte pre-pad and post-pad */
    const uint8_t *src_pcm = raw + DOOM_HEADER_SIZE + DOOM_PAD_BYTES;
    uint32_t usable = src_len - DOOM_PAD_BYTES * 2;
    if (usable == 0) {
        W_ReleaseLumpNum(lumpnum);
        return NULL;
    }

    /* Calculate resampled length */
    uint32_t dst_len = (uint32_t)((uint64_t)usable * TARGET_RATE / src_rate) + 1;

    int16_t *dst = (int16_t *)malloc(dst_len * sizeof(int16_t));
    if (!dst) {
        W_ReleaseLumpNum(lumpnum);
        return NULL;
    }

    /* Resample with linear interpolation: src_rate → TARGET_RATE
     * Using fixed-point 16.16 arithmetic to avoid floats. */
    uint32_t step = ((uint32_t)src_rate << 16) / TARGET_RATE;  /* advance per output sample */
    uint32_t frac = 0;
    uint32_t actual_len = 0;

    for (uint32_t i = 0; i < dst_len; i++) {
        uint32_t idx = frac >> 16;
        if (idx >= usable - 1) break;

        uint32_t f = frac & 0xFFFF;
        /* Convert unsigned 8-bit to signed 16-bit */
        int32_t s0 = ((int32_t)src_pcm[idx]     - 128) << 8;
        int32_t s1 = ((int32_t)src_pcm[idx + 1]  - 128) << 8;
        int32_t sample = s0 + (int32_t)(((int64_t)(s1 - s0) * f) >> 16);

        dst[i] = (int16_t)sample;
        actual_len++;
        frac += step;
    }

    W_ReleaseLumpNum(lumpnum);

    /* Store in cache */
    if (cache_count < MAX_CACHED) {
        cached_sound_t *c = &cache[cache_count++];
        c->lumpnum = lumpnum;
        c->data    = dst;
        c->length  = actual_len;
        return c;
    }

    /* Cache full — still return a temporary (will leak; unlikely to hit) */
    static cached_sound_t tmp;
    if (tmp.data) free(tmp.data);
    tmp.lumpnum = lumpnum;
    tmp.data    = dst;
    tmp.length  = actual_len;
    return &tmp;
}

/* ------------------------------------------------------------------ */
/*  sound_module_t implementation                                      */
/* ------------------------------------------------------------------ */

static boolean edgetx_Init(boolean use_sfx_prefix)
{
    (void)use_sfx_prefix;
    memset(channels, 0, sizeof(channels));
    cache_count = 0;

    /* Set speaker volume (range 0-VOLUME_LEVEL_MAX, 12 is default) */
    audioSetVolume(VOLUME_LEVEL_DEF);

    DOOM_LOG("[sound] EdgeTX sound module initialised (rate=%d)\r\n",
             TARGET_RATE);
    return true;
}

static void edgetx_Shutdown(void)
{
    for (int i = 0; i < NUM_CHANNELS; i++)
        channels[i].active = false;

    for (int i = 0; i < cache_count; i++) {
        free(cache[i].data);
        cache[i].data = NULL;
    }
    cache_count = 0;
}

static int edgetx_GetSfxLumpNum(sfxinfo_t *sfx)
{
    char namebuf[9];
    /* DOOM SFX lumps are named "DS<name>" */
    namebuf[0] = 'D';
    namebuf[1] = 'S';
    strncpy(namebuf + 2, sfx->name, 6);
    namebuf[8] = '\0';

    int num = W_CheckNumForName(namebuf);
    return num;
}

static void edgetx_Update(void)
{
    /* Mix all active channels into AudioBuffers and push to the FIFO.
     * We produce as many buffers as the FIFO can accept (AUDIO_BUFFER_SIZE
     * samples = 10ms each).  This is called once per game frame (~35 fps)
     * so we need to fill enough buffers to cover the inter-frame gap. */

    for (;;) {
        AudioBuffer *buf = audioQueue.buffersFifo.getEmptyBuffer();
        if (!buf) {
            /* FIFO full — DMA ISR will drain it */
            break;
        }

        /* Check if there are any active channels */
        boolean any_active = false;
        for (int ch = 0; ch < NUM_CHANNELS; ch++) {
            if (channels[ch].active) {
                any_active = true;
                break;
            }
        }

        /* Don't push silent buffers — avoids wasting DMA bandwidth
         * and lets the I2S stop when idle */
        if (!any_active)
            break;

        /* Zero the buffer */
        memset(buf->data, 0, sizeof(buf->data));
        buf->size = AUDIO_BUFFER_SIZE;

        /* Mix each active channel */
        for (int ch = 0; ch < NUM_CHANNELS; ch++) {
            channel_t *c = &channels[ch];
            if (!c->active || !c->data)
                continue;

            /* Volume scaling: DOOM volume is 0-127.
             * We map to a multiplier: vol / 127, applied as (sample * vol) >> 7 */
            int vol = c->vol;
            if (vol <= 0) {
                c->active = false;
                continue;
            }

            uint32_t remaining = c->length - c->pos;
            uint32_t to_mix = (remaining < AUDIO_BUFFER_SIZE) ? remaining : AUDIO_BUFFER_SIZE;

            const int16_t *src = c->data + c->pos;
            for (uint32_t i = 0; i < to_mix; i++) {
                int32_t s = ((int32_t)src[i] * vol) >> 7;
                /* Accumulate (mix) — clamp after all channels */
                int32_t mixed = (int32_t)buf->data[i] + s;
                if (mixed > 32767)  mixed = 32767;
                if (mixed < -32768) mixed = -32768;
                buf->data[i] = (audio_data_t)mixed;
            }

            c->pos += to_mix;
            if (c->pos >= c->length)
                c->active = false;
        }

        audioQueue.buffersFifo.audioPushBuffer();

    } /* end for(;;) */

    /* Kick the DMA if it isn't running yet */
    audioConsumeCurrentBuffer();
}

static void edgetx_UpdateSoundParams(int channel, int vol, int sep)
{
    if (channel < 0 || channel >= NUM_CHANNELS)
        return;
    channels[channel].vol = vol;
    channels[channel].sep = sep;
}

static int edgetx_StartSound(sfxinfo_t *sfx, int channel, int vol, int sep)
{
    if (channel < 0 || channel >= NUM_CHANNELS)
        return -1;

    /* Resolve lump number if needed */
    if (sfx->lumpnum < 0)
        sfx->lumpnum = edgetx_GetSfxLumpNum(sfx);

    if (sfx->lumpnum < 0)
        return -1;

    cached_sound_t *cs = get_cached_sound(sfx->lumpnum);
    if (!cs || !cs->data || cs->length == 0)
        return -1;

    channel_t *c  = &channels[channel];
    c->data       = cs->data;
    c->length     = cs->length;
    c->pos        = 0;
    c->vol        = vol;
    c->sep        = sep;
    c->active     = true;

    return channel;
}

static void edgetx_StopSound(int channel)
{
    if (channel >= 0 && channel < NUM_CHANNELS)
        channels[channel].active = false;
}

static boolean edgetx_SoundIsPlaying(int channel)
{
    if (channel < 0 || channel >= NUM_CHANNELS)
        return false;
    return channels[channel].active;
}

/* ------------------------------------------------------------------ */
/*  Module descriptor                                                  */
/* ------------------------------------------------------------------ */

static snddevice_t sound_edgetx_devices[] = {
    SNDDEVICE_SB,       /* default device in i_sound.c */
};

sound_module_t sound_edgetx_module = {
    sound_edgetx_devices,
    sizeof(sound_edgetx_devices) / sizeof(*sound_edgetx_devices),
    edgetx_Init,
    edgetx_Shutdown,
    edgetx_GetSfxLumpNum,
    edgetx_Update,
    edgetx_UpdateSoundParams,
    edgetx_StartSound,
    edgetx_StopSound,
    edgetx_SoundIsPlaying,
    NULL,  /* CacheSounds — optional */
};
