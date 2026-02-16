/*
 * DOOM sound module for EdgeTX / TX-15
 *
 * Plays DOOM SFX through the TX-15 integrated speaker via TAS2505 / I2S.
 * Sound effects are loaded from WAD lumps, resampled from 11025 Hz (DOOM
 * native) to 32 kHz (EdgeTX I2S rate), and mixed in software.
 *
 * Music playback is not implemented (no OPL / MIDI synth).
 *
 * License: GPLv2
 */

#ifndef DOOM_EDGETX_SOUND_H
#define DOOM_EDGETX_SOUND_H

#include "i_sound.h"

#ifdef __cplusplus
extern "C" {
#endif

extern sound_module_t sound_edgetx_module;

#ifdef __cplusplus
}
#endif

#endif /* DOOM_EDGETX_SOUND_H */
