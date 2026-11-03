/*
 * sound.h - music and sound effects (see sound.c).
 */
#ifndef SOUND_H
#define SOUND_H

#include <stdint.h>
#include "music.h"

/* 16ths of the music played so far, and beats (for the animations) */
extern uint8_t music_step;
extern uint8_t beat;

void sound_init(void);
void sound_tick(void);
void music_start(void);
void music_resume(void);
void music_stop(void);
uint8_t music_playing(void);
void sfx_play(uint8_t n);

#endif
