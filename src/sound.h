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

/* the song playing (0..SONGS-1), and the one chosen in the menu (saved): a game starts with it,
   every new shift plays the next one */
extern uint8_t music_song, song_choice;
uint8_t music_playing(void);
void sfx_play(uint8_t n);

#endif
