/*
 * DOOM music module for EdgeTX / TX-15
 *
 * Parses MUS format music data from WAD lumps and synthesises audio using
 * simple square-wave / noise synthesis.  The generated PCM is mixed into
 * the SFX audio buffer by the sound module (sound.cpp).
 *
 * License: GPLv2
 */

#ifndef DOOM_EDGETX_MUSIC_H
#define DOOM_EDGETX_MUSIC_H

#ifdef __cplusplus
extern "C" {
#endif

#include "i_sound.h"

extern music_module_t music_edgetx_module;

/*
 * Called from edgetx_Update() in sound.cpp to mix synthesised music
 * samples into an existing audio buffer.
 *   buffer — pre-filled with SFX data (int16_t, signed 16-bit)
 *   count  — number of samples in the buffer
 */
void music_mix_into_buffer(int16_t *buffer, uint32_t count);

/* Returns true when music is actively generating audio. */
boolean music_is_generating(void);

#ifdef __cplusplus
}
#endif

#endif /* DOOM_EDGETX_MUSIC_H */
