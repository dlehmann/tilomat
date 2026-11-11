/*
 * sound.c - music and sound effects on the Game Boy's sound chip.
 *
 * The music is ten songs (gen/music.c), stored in one-bar patterns per channel. A song is one
 * event per 16th note, every step_fp / 256 ticks: on each channel a note starts, ends (1) or
 * holds (0). Channel 1 plays the melody, channel 2 a second
 * voice below it, channel 3 the bass, channel 4 the drums. The effects are small programs for
 * channel 1, the melody pauses while one plays; noise in an effect borrows channel 4.
 */
#include <gb/gb.h>
#include <gb/hardware.h>
#include "sound.h"
#include "music.h"

static uint8_t playing;
static uint16_t step_acc;

static const uint8_t *sfx;
static uint8_t sfx_wait, sfx_vol, sfx_duty, sfx_noise, sfx_prio_now;
static uint16_t slide_x, slide_end;
static int16_t slide_d;
static uint8_t slide_n;

/* a triangle for the bass */
static const uint8_t wave[16] = {
    0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, 0xfe, 0xdc, 0xba, 0x98, 0x76, 0x54, 0x32, 0x10
};

void sound_init(void)
{
    uint8_t i;

    NR52_REG = 0x80;
    NR50_REG = 0x77;
    NR51_REG = 0xff;
    NR30_REG = 0;
    for (i = 0; i < 16; i++)
        AUD3WAVE[i] = wave[i];
    NR12_REG = 0;
    NR22_REG = 0;
    NR32_REG = 0;
    NR42_REG = 0;
}

static void drum(uint8_t d)
{
    switch (d) {
    case 1:             /* kick */
        NR42_REG = 0xa1;
        NR43_REG = 0x72;
        break;
    case 2:             /* clap */
        NR42_REG = 0x92;
        NR43_REG = 0x52;
        break;
    case 3:             /* closed hi-hat */
        NR42_REG = 0x41;
        NR43_REG = 0x10;
        break;
    case 4:             /* open hi-hat */
        NR42_REG = 0x53;
        NR43_REG = 0x11;
        break;
    default:
        return;
    }
    NR41_REG = 0x3f;
    NR44_REG = 0x80;
}

/* a note on a pulse channel, reg: its registers (NR10 or NR20, which is one before NR21) */
#define CH1 ((volatile uint8_t *)0xff10)
#define CH2 ((volatile uint8_t *)0xff15)

static void pulse(volatile uint8_t *reg, uint8_t n, uint8_t duty, uint8_t env)
{
    if (n == 1) {
        reg[2] = 0;
        reg[4] = 0x80;
    } else {
        uint16_t x = note_pulse[n - 24];
        reg[1] = duty;
        reg[2] = env;
        reg[3] = (uint8_t)x;
        reg[4] = 0x80 | (x >> 8);
    }
}

/* the song playing, and in it the patterns of the bar playing */
uint8_t music_song, song_choice;
static const SongInfo *song;
static uint8_t bar, step;
static const uint8_t *row_lead, *row_harm, *row_bass, *row_drum;

static void music_event(void)
{
    uint8_t l, v, b;

    if (step == 0) {
        const uint8_t *o = song->order + bar;
        uint8_t n = song->bars;
        row_lead = pat_lead + ((uint16_t)o[0] << 4);
        row_harm = pat_harm + ((uint16_t)o[n] << 4);
        row_bass = pat_bass + ((uint16_t)o[2 * n] << 4);
        row_drum = pat_drum + ((uint16_t)o[3 * n] << 4);
    }
    l = row_lead[step];
    v = row_harm[step];
    b = row_bass[step];
    if (b == 1) {
        NR32_REG = 0;
    } else if (b) {
        uint16_t x = note_wave[b - 24];
        /* the DAC off stops the channel: retriggering it while it plays can garble the wave RAM */
        NR30_REG = 0;
        NR30_REG = 0x80;
        NR32_REG = 0x20;
        NR33_REG = (uint8_t)x;
        NR34_REG = 0x80 | (x >> 8);
    }
    if (!sfx_noise)
        drum(row_drum[step]);
    if (l && !sfx) {
        NR10_REG = 0;
        pulse(CH1, l, song->lead_duty, song->lead_env);
    }
    if (v)
        pulse(CH2, v, song->harm_duty, song->harm_env);
    if (++step == 16) {
        step = 0;
        if (++bar == song->bars)
            bar = 0;
    }
}

void music_start(void)
{
    if (playing)
        return;
    song = &songs[music_song];
    playing = 1;
    bar = step = 0;
    step_acc = song->step_fp;
}

/* continues where the music was stopped (after the pause) */
void music_resume(void)
{
    playing = 1;
}

/* switches to song n and plays it from the start */
void music_select(uint8_t n)
{
    music_stop();
    music_song = n;
    music_start();
}

void music_stop(void)
{
    playing = 0;
    NR22_REG = 0;
    NR24_REG = 0x80;
    NR32_REG = 0;
    if (!sfx) {
        NR12_REG = 0;
        NR14_REG = 0x80;
    }
    if (!sfx_noise) {
        NR42_REG = 0;
        NR44_REG = 0x80;
    }
}

uint8_t music_playing(void)
{
    return playing;
}

void sfx_play(uint8_t n)
{
    if (sfx && sfx_prio[n] < sfx_prio_now)
        return;
    sfx_prio_now = sfx_prio[n];
    if (sfx_noise) {
        NR42_REG = 0;
        NR44_REG = 0x80;
        sfx_noise = 0;
    }
    sfx = sfx_programs[n - 1];
    sfx_wait = 0;
    slide_n = 0;
    sfx_vol = 0xa0;
    sfx_duty = 0x80;
}

static void ch1(uint16_t x, uint8_t trigger)
{
    if (trigger) {
        NR10_REG = 0;
        NR11_REG = sfx_duty;
        NR12_REG = sfx_vol;
    }
    NR13_REG = (uint8_t)x;
    NR14_REG = (trigger ? 0x80 : 0) | (x >> 8);
}

static void sfx_tick(void)
{
    uint8_t c;

    if (slide_n) {
        slide_x += slide_d;
        if (!--slide_n)
            slide_x = slide_end;
        ch1(slide_x, 0);
    }
    if (sfx_wait && --sfx_wait)
        return;
    if (!sfx)
        return;
    for (;;) {
        c = *sfx++;
        switch (c) {
        case SC_VOL:
            sfx_vol = *sfx++;
            continue;
        case SC_DUTY:
            sfx_duty = *sfx++;
            continue;
        case SC_NOTE:
            if (sfx_noise) {
                NR42_REG = 0;
                NR44_REG = 0x80;
                sfx_noise = 0;
            }
            slide_n = 0;
            ch1(((uint16_t)sfx[0] << 8) | sfx[1], 1);
            sfx_wait = sfx[2];
            sfx += 3;
            return;
        case SC_SLIDE:
            slide_x = ((uint16_t)sfx[0] << 8) | sfx[1];
            slide_end = ((uint16_t)sfx[2] << 8) | sfx[3];
            sfx_wait = slide_n = sfx[4];
            slide_d = ((int16_t)slide_end - (int16_t)slide_x) / (int16_t)slide_n;
            ch1(slide_x, 1);
            sfx += 5;
            return;
        case SC_NOISE:
            sfx_noise = 1;
            NR42_REG = sfx_vol;
            NR43_REG = sfx[0];
            NR41_REG = 0x3f;
            NR44_REG = 0x80;
            sfx_wait = sfx[1];
            sfx += 2;
            return;
        case SC_OFF:
            slide_n = 0;
            NR12_REG = 0;
            NR14_REG = 0x80;
            if (sfx_noise) {
                NR42_REG = 0;
                NR44_REG = 0x80;
                sfx_noise = 0;
            }
            sfx_wait = *sfx++;
            return;
        default:
            NR12_REG = 0;
            NR14_REG = 0x80;
            if (sfx_noise) {
                NR42_REG = 0;
                NR44_REG = 0x80;
                sfx_noise = 0;
            }
            sfx = 0;
            return;
        }
    }
}

void sound_tick(void)
{
    sfx_tick();
    if (!playing)
        return;
    step_acc += 256;
    if (step_acc >= song->step_fp) {
        step_acc -= song->step_fp;
        music_event();
    }
}
