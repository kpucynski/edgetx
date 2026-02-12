/*
 * DOOM music module for EdgeTX / TX-15
 *
 * Parses MUS-format music data from DOOM WAD lumps and renders audio
 * using lightweight square-wave synthesis (melodic channels) and LFSR
 * noise (percussion channel 15).
 *
 * The MUS sequencer is advanced sample-by-sample inside
 * music_mix_into_buffer(), which is called from the SFX module's
 * edgetx_Update().  This keeps music and SFX perfectly synchronised
 * in a single audio pipeline.
 *
 * License: GPLv2
 */

#include "music.h"

#include <string.h>
#include <stdlib.h>

extern "C" {
#include "doomtype.h"
}
#include "doomgeneric.h"

#include "hal/audio_driver.h"   /* AUDIO_SAMPLE_RATE (32000) */

/* ------------------------------------------------------------------ */
/*  Constants                                                          */
/* ------------------------------------------------------------------ */

#define MUS_MAX_CHANNELS   16
#define PERCUSSION_CHAN     15
#define MUS_TICKS_PER_SEC  140

/* MUS event types */
#define MUS_EV_RELEASE     0
#define MUS_EV_PLAY        1
#define MUS_EV_PITCH       2
#define MUS_EV_SYSTEM      3
#define MUS_EV_CONTROLLER  4
#define MUS_EV_END         6

/* Fixed-point 16.16 timing:
 *   samples_per_tick = SAMPLE_RATE * 65536 / 140                    */
#define SAMPLES_PER_TICK_FP \
    ((uint32_t)((uint64_t)AUDIO_SAMPLE_RATE * 65536U / MUS_TICKS_PER_SEC))

/* ------------------------------------------------------------------ */
/*  Note-frequency table  (phase-increment per sample, 16.16 FP)      */
/* ------------------------------------------------------------------ */

static uint32_t note_phase_inc[128];
static bool tables_ready = false;

static void init_tables(void)
{
    if (tables_ready) return;

    /* A4 = MIDI note 69 = 440 Hz
     * phase_inc = freq * 65536 / SAMPLE_RATE
     * A4: 440 * 65536 / 32000 = 901                                */
    note_phase_inc[69] = 901;

    /* Go up: multiply by 2^(1/12) ≈ 69433/65536 each semitone      */
    uint32_t inc = 901;
    for (int n = 70; n < 128; n++) {
        inc = (uint32_t)(((uint64_t)inc * 69433 + 32768) >> 16);
        note_phase_inc[n] = inc;
    }

    /* Go down: multiply by 2^(-1/12) ≈ 61858/65536                 */
    inc = 901;
    for (int n = 68; n >= 0; n--) {
        inc = (uint32_t)(((uint64_t)inc * 61858 + 32768) >> 16);
        note_phase_inc[n] = inc;
    }

    tables_ready = true;
}

/* ------------------------------------------------------------------ */
/*  Per-channel voice state                                            */
/* ------------------------------------------------------------------ */

typedef struct {
    bool     active;
    uint8_t  note;          /* MIDI note 0-127                       */
    uint8_t  volume;        /* channel volume 0-127                  */
    uint8_t  pitch_bend;    /* 0-255, centre = 128                   */
    uint32_t phase;         /* waveform phase accumulator            */
    int16_t  env;           /* envelope 0-256                        */
    bool     releasing;     /* in release phase                      */
} mus_voice_t;

/* ------------------------------------------------------------------ */
/*  Music playback state                                               */
/* ------------------------------------------------------------------ */

static struct {
    const uint8_t *score;       /* start of score data               */
    uint32_t       score_len;
    uint32_t       pos;         /* current read position in score    */
    uint32_t       delay;       /* ticks remaining before next event */

    bool           playing;
    bool           looping;
    bool           paused;
    int            volume;      /* master music volume 0-127         */

    const uint8_t *data;        /* full MUS lump (for rewind)        */
    uint32_t       data_len;
    uint32_t       score_offset;

    mus_voice_t    ch[MUS_MAX_CHANNELS];
    uint8_t        ch_vol[MUS_MAX_CHANNELS]; /* last-set volume      */

    uint32_t       tick_frac;   /* sub-tick accumulator (16.16 FP)   */
    uint32_t       noise_lfsr;  /* LFSR state for percussion         */
} mus;

/* ------------------------------------------------------------------ */
/*  MUS parser helpers                                                 */
/* ------------------------------------------------------------------ */

static uint32_t mus_read_delay(void)
{
    uint32_t d = 0;
    uint8_t  b;
    do {
        if (mus.pos >= mus.score_len) return 0;
        b = mus.score[mus.pos++];
        d = (d << 7) | (b & 0x7F);
    } while (b & 0x80);
    return d;
}

static void mus_process_event(void)
{
    if (mus.pos >= mus.score_len) {
        if (mus.looping) { mus.pos = 0; mus.delay = 0; }
        else             { mus.playing = false; }
        return;
    }

    uint8_t ev   = mus.score[mus.pos++];
    bool    last  = (ev & 0x80) != 0;
    uint8_t type  = (ev >> 4) & 0x07;
    uint8_t chan  = ev & 0x0F;
    mus_voice_t *v = &mus.ch[chan];

    switch (type) {

    case MUS_EV_RELEASE:
        if (mus.pos < mus.score_len) mus.pos++;   /* skip note byte */
        v->releasing = true;
        break;

    case MUS_EV_PLAY: {
        if (mus.pos >= mus.score_len) break;
        uint8_t nb   = mus.score[mus.pos++];
        bool has_vol  = (nb & 0x80) != 0;
        uint8_t note  = nb & 0x7F;

        if (has_vol && mus.pos < mus.score_len) {
            mus.ch_vol[chan] = mus.score[mus.pos++] & 0x7F;
        }

        if (note < 128) {
            v->note      = note;
            v->volume    = mus.ch_vol[chan];
            v->active    = true;
            v->releasing = false;
            v->phase     = 0;
            v->env       = (chan == PERCUSSION_CHAN) ? 256 : 1;
        }
        break;
    }

    case MUS_EV_PITCH:
        if (mus.pos < mus.score_len) v->pitch_bend = mus.score[mus.pos++];
        break;

    case MUS_EV_SYSTEM:
        if (mus.pos < mus.score_len) {
            uint8_t ctrl = mus.score[mus.pos++] & 0x7F;
            if (ctrl == 10 || ctrl == 11) {        /* all sounds/notes off */
                v->active = false;
                v->env    = 0;
            }
        }
        break;

    case MUS_EV_CONTROLLER:
        if (mus.pos + 1 < mus.score_len) {
            uint8_t ctrl = mus.score[mus.pos++] & 0x7F;
            uint8_t val  = mus.score[mus.pos++] & 0x7F;
            if (ctrl == 3) {                       /* volume */
                mus.ch_vol[chan] = val;
                v->volume        = val;
            }
            /* ctrl 0 = patch change – ignored (no timbres) */
            /* ctrl 4 = pan        – ignored (mono speaker) */
        }
        break;

    case MUS_EV_END:
        if (mus.looping) {
            mus.pos   = 0;
            mus.delay = 0;
            for (int i = 0; i < MUS_MAX_CHANNELS; i++)
                mus.ch[i].releasing = true;
        } else {
            mus.playing = false;
        }
        return;                          /* no delay after score-end */

    default:
        break;
    }

    if (last)
        mus.delay = mus_read_delay();
}

/* Advance sequencer by one MUS tick (1/140 s). */
static void mus_advance_tick(void)
{
    if (!mus.playing || mus.paused) return;

    if (mus.delay > 0) { mus.delay--; return; }

    int safety = 1000;
    while (mus.delay == 0 && mus.playing && --safety > 0)
        mus_process_event();
}

/* ------------------------------------------------------------------ */
/*  Waveform synthesis (per-sample)                                    */
/* ------------------------------------------------------------------ */

static inline int16_t synth_melodic(mus_voice_t *v)
{
    if (v->env <= 0) return 0;

    /* Phase increment with pitch-bend (±2 semitones over ±128) */
    uint32_t inc = (v->note < 128) ? note_phase_inc[v->note] : 0;
    if (v->pitch_bend != 128) {
        int32_t bend = (int32_t)v->pitch_bend - 128;
        int32_t adj  = 65536 + bend * 59;           /* ≈2 semitones */
        inc = (uint32_t)(((uint64_t)inc * (uint32_t)adj) >> 16);
    }

    v->phase += inc;

    /* 50 % duty-cycle square wave, ±8192 amplitude */
    int16_t sample = (v->phase & 0x8000) ? 8192 : -8192;

    /* Volume × envelope */
    int32_t s = ((int32_t)sample * v->volume) >> 7;
    s = (s * v->env) >> 8;

    /* Envelope update */
    if (v->releasing) {
        v->env -= 3;
        if (v->env <= 0) { v->env = 0; v->active = false; }
    } else if (v->env < 256) {
        v->env += 32;
        if (v->env > 256) v->env = 256;
    }

    return (int16_t)s;
}

static inline int16_t synth_percussion(mus_voice_t *v, uint32_t *lfsr)
{
    if (v->env <= 0) return 0;

    /* 16-bit Galois LFSR */
    uint32_t l = *lfsr;
    l = (l >> 1) ^ (-(int32_t)(l & 1) & 0xB400u);
    *lfsr = l;

    int16_t sample = (l & 1) ? 6000 : -6000;

    int32_t s = ((int32_t)sample * v->volume) >> 7;
    s = (s * v->env) >> 8;

    /* fast decay */
    v->env -= 2;
    if (v->env <= 0) { v->env = 0; v->active = false; }

    return (int16_t)s;
}

/* ------------------------------------------------------------------ */
/*  Public: mix music samples into an existing buffer                  */
/* ------------------------------------------------------------------ */

extern "C" void music_mix_into_buffer(int16_t *buffer, uint32_t count)
{
    if (!mus.playing || mus.paused) return;

    for (uint32_t i = 0; i < count; i++) {
        /* Advance tick timer */
        mus.tick_frac += 65536;
        while (mus.tick_frac >= SAMPLES_PER_TICK_FP) {
            mus.tick_frac -= SAMPLES_PER_TICK_FP;
            mus_advance_tick();
            if (!mus.playing) return;
        }

        /* Synthesise all channels */
        int32_t mix = 0;
        for (int ch = 0; ch < MUS_MAX_CHANNELS; ch++) {
            mus_voice_t *v = &mus.ch[ch];
            if (!v->active && v->env <= 0) continue;

            if (ch == PERCUSSION_CHAN)
                mix += synth_percussion(v, &mus.noise_lfsr);
            else
                mix += synth_melodic(v);
        }

        /* Master music volume and headroom scaling */
        mix = (mix * mus.volume) / 127;
        mix >>= 2;

        /* Accumulate into SFX buffer with clamping */
        int32_t out = (int32_t)buffer[i] + mix;
        if (out >  32767) out =  32767;
        if (out < -32768) out = -32768;
        buffer[i] = (int16_t)out;
    }
}

extern "C" boolean music_is_generating(void)
{
    return mus.playing && !mus.paused;
}

/* ------------------------------------------------------------------ */
/*  music_module_t implementation                                      */
/* ------------------------------------------------------------------ */

static boolean edgetx_music_Init(void)
{
    init_tables();
    memset(&mus, 0, sizeof(mus));
    mus.volume     = 127;
    mus.noise_lfsr = 1;

    DOOM_LOG("[music] EdgeTX music module initialised\r\n");
    return true;
}

static void edgetx_music_Shutdown(void)
{
    mus.playing = false;
}

static void edgetx_music_SetMusicVolume(int volume)
{
    if (volume < 0)   volume = 0;
    if (volume > 127) volume = 127;
    mus.volume = volume;
}

static void edgetx_music_PauseMusic(void)  { mus.paused = true;  }
static void edgetx_music_ResumeMusic(void) { mus.paused = false; }

static void *edgetx_music_RegisterSong(void *data, int len)
{
    if (!data || len < 16) return NULL;

    const uint8_t *raw = (const uint8_t *)data;

    /* Verify MUS header "MUS\x1a" */
    if (raw[0] != 'M' || raw[1] != 'U' ||
        raw[2] != 'S' || raw[3] != 0x1A) {
        DOOM_LOG("[music] Not a MUS file\r\n");
        return NULL;
    }

    uint16_t score_start = raw[6] | (raw[7] << 8);
    if (score_start >= (uint16_t)len) {
        DOOM_LOG("[music] Invalid score offset\r\n");
        return NULL;
    }

    mus.data         = raw;
    mus.data_len     = (uint32_t)len;
    mus.score_offset = score_start;
    mus.score        = raw + score_start;
    mus.score_len    = (uint32_t)len - score_start;

    DOOM_LOG("[music] Registered MUS (offset %u, len %u)\r\n",
             score_start, mus.score_len);
    return (void *)&mus;
}

static void edgetx_music_UnRegisterSong(void *handle)
{
    (void)handle;
    mus.playing = false;
    mus.data    = NULL;
    mus.score   = NULL;
}

static void edgetx_music_PlaySong(void *handle, boolean looping)
{
    (void)handle;
    if (!mus.score) return;

    mus.pos       = 0;
    mus.delay     = 0;
    mus.looping   = looping;
    mus.paused    = false;
    mus.tick_frac = 0;

    memset(mus.ch, 0, sizeof(mus.ch));
    for (int i = 0; i < MUS_MAX_CHANNELS; i++)
        mus.ch_vol[i] = 100;

    mus.noise_lfsr = 1;
    mus.playing    = true;

    DOOM_LOG("[music] Playing%s\r\n", looping ? " (loop)" : "");
}

static void edgetx_music_StopSong(void)
{
    mus.playing = false;
    for (int i = 0; i < MUS_MAX_CHANNELS; i++) {
        mus.ch[i].active = false;
        mus.ch[i].env    = 0;
    }
}

static boolean edgetx_music_MusicIsPlaying(void)
{
    return mus.playing;
}

static void edgetx_music_Poll(void)
{
    /* Synthesis is driven by music_mix_into_buffer() –
     * called from the SFX update path.  Nothing to do here. */
}

/* ------------------------------------------------------------------ */
/*  Module descriptor                                                  */
/* ------------------------------------------------------------------ */

static snddevice_t music_edgetx_devices[] = {
    SNDDEVICE_SB,
};

music_module_t music_edgetx_module = {
    music_edgetx_devices,
    sizeof(music_edgetx_devices) / sizeof(*music_edgetx_devices),
    edgetx_music_Init,
    edgetx_music_Shutdown,
    edgetx_music_SetMusicVolume,
    edgetx_music_PauseMusic,
    edgetx_music_ResumeMusic,
    edgetx_music_RegisterSong,
    edgetx_music_UnRegisterSong,
    edgetx_music_PlaySong,
    edgetx_music_StopSong,
    edgetx_music_MusicIsPlaying,
    edgetx_music_Poll,
};
