/*
 * sound.h - music and sound effects (see sound.c).
 */
#ifndef SOUND_H
#define SOUND_H

#include <stdint.h>
#include "music.h"

void sound_init(void);
void sound_tick(void);
void music_start(void);
void music_resume(void);
void music_stop(void);
void music_select(uint8_t n);

/* the song chosen (0..SONGS-1) */
extern uint8_t music_song;
uint8_t music_playing(void);
void sfx_play(uint8_t n);

#endif
