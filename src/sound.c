/*
 * sound.c - music and sound effects on the Game Boy's sound chip.
 *
 * The music (gen/music.c) is one event per 16th note, which takes MUS_STEP_A and MUS_STEP_B
 * ticks in turn. Channel 2 plays the chords (as a fast arpeggio) and the melody, channel 3
 * the bass, channel 4 the drums. The effects are small
 * programs for channel 1; noise in an effect borrows channel 4 from the drums.
 */
#include <gb/gb.h>
#include <gb/hardware.h>
#include "sound.h"
#include "music.h"

static uint8_t playing;
static uint8_t pos, wait, bass_gate, v2_gate, arp, arp_tick;
/* the beat counter for the animations: steps since the start */
uint8_t music_step;
uint8_t beat;

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

static void ch2_note(uint8_t n, uint8_t env)
{
    uint16_t x = note_pulse[n - 24];
    NR21_REG = 0x80;
    NR22_REG = env;
    NR23_REG = (uint8_t)x;
    NR24_REG = 0x80 | (x >> 8);
}

static void ch2_freq(uint8_t n)
{
    uint16_t x = note_pulse[n - 24];
    NR23_REG = (uint8_t)x;
    NR24_REG = x >> 8;
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

static void music_event(void)
{
    uint8_t b = mus_bass[pos], v = mus_v2[pos];

    if (b) {
        uint16_t x = note_wave[b - 24];
        NR30_REG = 0x80;
        NR32_REG = 0x40;
        NR33_REG = (uint8_t)x;
        NR34_REG = 0x80 | (x >> 8);
        bass_gate = 6;
    }
    if (!sfx_noise)
        drum(mus_drum[pos]);
    if (v) {
        v2_gate = mus_v2len[pos] * 7 + (mus_v2len[pos] >> 1);
        if (v & 0x80) {
            arp = 1 + (v & 0x7f) * 3;
            arp_tick = 0;
            ch2_note(mus_chord[arp - 1], 0x72);
        } else {
            arp = 0;
            ch2_note(v, 0x83);
        }
    }
    music_step++;
    beat = music_step >> 2;
    pos++;
}

void music_start(void)
{
    if (playing)
        return;
    playing = 1;
    pos = 0;
    wait = 1;
    v2_gate = bass_gate = 0;
}

/* continues where the music was stopped (after the pause) */
void music_resume(void)
{
    playing = 1;
}

void music_stop(void)
{
    playing = 0;
    NR22_REG = 0;
    NR24_REG = 0x80;
    NR32_REG = 0;
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
    if (bass_gate && !--bass_gate)
        NR32_REG = 0;
    if (v2_gate && !--v2_gate) {
        NR22_REG = 0;
        NR24_REG = 0x80;
        arp = 0;
    }
    if (arp) {
        uint8_t base = arp - 1 - (arp - 1) % 3;
        arp_tick = arp_tick == 2 ? 0 : arp_tick + 1;
        ch2_freq(mus_chord[base + arp_tick]);
    }
    if (!--wait) {
        music_event();
        wait = (pos & 1) ? MUS_STEP_A : MUS_STEP_B;
    }
}
