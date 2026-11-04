/*
 * sound.c - music and sound effects on the Game Boy's sound chip.
 *
 * The music is ten songs (gen/songs*.c, in the ROM banks from 2 on); the one playing is copied
 * into RAM. A song is one event per 16th note, every step_fp / 256 ticks: on each channel a
 * note starts, ends (1) or holds (0). Channel 1 plays the melody, channel 2 a second
 * voice below it, channel 3 the bass, channel 4 the drums. The effects are small programs for
 * channel 1, the melody pauses while one plays; noise in an effect borrows channel 4.
 */
#include <gb/gb.h>
#include <gb/hardware.h>
#include "sound.h"
#include "music.h"

static uint8_t playing;
static uint16_t pos, step_acc;

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

/* the song playing, copied from its ROM bank: lead, harm, bass and drum of `steps` 16ths each */
uint8_t music_song, song_choice;
static uint8_t loaded = 0xff;
static uint8_t song_buf[4 * SONG_MAX_STEPS];
static uint16_t steps, step_fp;
static uint8_t lead_duty, lead_env, harm_duty, harm_env;

/* in bank 0 (and with the interrupts off), as the bank switched in replaces $4000-$7fff */
static void copy_banked(uint8_t *dst, const uint8_t *src, uint16_t n, uint8_t bank) NONBANKED
{
    uint8_t save = _current_bank;

    disable_interrupts();
    SWITCH_ROM(bank);
    while (n--)
        *dst++ = *src++;
    SWITCH_ROM(save);
    enable_interrupts();
}

static void load(void)
{
    const SongInfo *s = &songs[music_song];

    if (loaded == music_song)
        return;
    loaded = music_song;
    steps = s->steps;
    step_fp = s->step_fp;
    lead_duty = s->lead_duty;
    lead_env = s->lead_env;
    harm_duty = s->harm_duty;
    harm_env = s->harm_env;
    copy_banked(song_buf, s->data, 4 * steps, s->bank);
}

static void music_event(void)
{
    const uint8_t *p = song_buf + pos;
    uint8_t l = p[0], v = p[steps], b = p[2 * steps];

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
        drum(p[3 * steps]);
    if (l && !sfx) {
        NR10_REG = 0;
        pulse(CH1, l, lead_duty, lead_env);
    }
    if (v)
        pulse(CH2, v, harm_duty, harm_env);
    if (++pos == steps)
        pos = 0;
}

void music_start(void)
{
    if (playing)
        return;
    load();
    playing = 1;
    pos = 0;
    step_acc = step_fp;
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
    if (step_acc >= step_fp) {
        step_acc -= step_fp;
        music_event();
    }
}
