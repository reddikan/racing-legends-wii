/*
 * RACING LEGENDS - synthesized racing sounds (no music, no audio files).
 *
 * Every sound in game.h's SND_* list is generated at start-up: engines from firing pulses through exhaust
 * resonances, tyres from resonant noise with a wobbling squeal, nitro from roaring jets of noise, impacts
 * from thumps and inharmonic metal. Loops are seamless so the platform can play them forever and change
 * only their pitch and volume. Samples are 16-bit signed mono at AUDIO_RATE in the host's byte order.
 */
#ifndef AUDIO_H
#define AUDIO_H

#include <stdint.h>

#define AUDIO_RATE 32000

void           audio_init(void);
const int16_t *audio_data(int snd);          /* 32-byte aligned */
int            audio_len(int snd);           /* samples (multiple of 16) */
int            audio_is_loop(int snd);
float          audio_gain(int snd);          /* suggested playback gain 0..1 */
const int16_t *audio_pool(void);
int            audio_pool_bytes(void);

#endif
