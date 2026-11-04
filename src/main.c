/*
 * main.c - Tilomat: title screen, game, name entry.
 *
 * One game tick per frame (the Game Boy shows 59.7 frames per second, the game counts 60).
 * Controls: left/right move the cart (repeating while held), A drops a tile, B or up tosses
 * it back, down runs the belt faster, START pauses (SELECT in the pause ends the game),
 * SELECT on the title screen switches between German and English.
 */
#include <gb/gb.h>
#include <gb/hardware.h>
#include <string.h>
#include "game.h"
#include "render.h"
#include "sound.h"
#include "save.h"
#include "gfx.h"
#include "text.h"

#define CH(c) (T_FONT + (c) - 32)

#define REPEAT_DELAY 10     /* frames until a held direction repeats */
#define REPEAT_EVERY 4

static uint8_t keys, pressed;
/* vertical blanks so far (counted in render.c), at the last frame */
extern volatile uint8_t vbl_count;
static uint8_t last_vbl;
static uint16_t frames;
/* the result of the last game for the title screen: score, and whether it is a record */
static uint32_t last_score;
static uint8_t last_record, last_played;

static void frame(void)
{
    uint8_t k;

    vsync();
    sound_tick();
    k = joypad();
    pressed = k & ~keys;
    keys = k;
    frames++;
}

/* ---------------------------------------------------------------- title screen */

#define BELT_ROW 9      /* the tiles on the belt of the title screen; the belt below */

static const uint8_t logo[] = {T_BIG_T, T_BIG_I, T_BIG_L, T_BIG_O, T_BIG_M, T_BIG_A, T_BIG_T};

static void draw_logo(void)
{
    uint8_t i, t;

    for (i = 0; i < sizeof(logo); i++) {
        t = logo[i];
        vset(0, 3 + i * 2, 1, t);
        vset(0, 4 + i * 2, 1, t + 1);
        vset(0, 3 + i * 2, 2, t + 2);
        vset(0, 4 + i * 2, 2, t + 3);
    }
}

static void draw_scores(void)
{
    uint8_t i, k;

    print_center(0, 4, TITLE_SCORES, 0);
    if (!scores[0].score) {
        print_center(0, 8, TITLE_NOSCORES, 0);
        return;
    }
    for (i = 0; i < SCORES && scores[i].score; i++) {
        print_num(0, 0, 6 + i, i + 1, 2);
        if (i < 9)
            vset(0, 0, 6 + i, T_SPACE);
        for (k = 0; k < NAME_LEN; k++)
            vset(0, 3 + k, 6 + i, scores[i].name[k]);
        print_num(0, 12, 6 + i, scores[i].score, 7);
    }
}

static void title_page(uint8_t page)
{
    vfill(0, 0, 4, 20, 13, T_SPACE);
    if (page) {
        render_hide_sprites();
        draw_scores();
    } else {
        title_belt_show(BELT_ROW);
        print_center(0, 5, TITLE_TAGLINE, 0);
        if (last_played)
            print_center(0, 12, last_record ? TITLE_RECORD : TITLE_SCORE, last_score);
    }
    print_center(0, 17, TITLE_LANG, 0);
}

static void title(void)
{
    uint8_t page = last_played && last_record ? 1 : 0;
    uint16_t t = 0;

    render_clear();
    draw_logo();
    title_page(page);
    music_start();
    for (;;) {
        frame();
        t++;
        if (t % 420 == 0) {
            page ^= 1;
            title_page(page);
        }
        if (!page && !(t & 1))
            title_belt_step();
        if ((t & 31) == 0)
            print_center(0, 15, TITLE_START, 0);
        else if ((t & 31) == 24)
            vfill(0, 0, 15, 20, 1, T_SPACE);
        if (pressed & J_SELECT) {
            lang ^= 1;
            save_store();
            sfx_play(SFX_KEY);
            title_page(page);
        }
        if (pressed & (J_START | J_A)) {
            sfx_play(SFX_SHIFT_START);
            return;
        }
    }
}

/* ---------------------------------------------------------------- the game */

static const uint8_t intro_goal[5] = {INTRO_G_LINES, INTRO_G_TILES, INTRO_G_KIND, INTRO_G_POINTS, INTRO_G_BIG};

/* the intro of a shift: its order, a new kind of tile, and in the first shift the controls */
static void intro_panel(void)
{
    uint8_t y = 4, h;

    if (g.shift == 1)
        h = 18;
    else
        h = 6 + (g.goal == GOAL_KIND ? 1 : 0) + (g.new_kind ? (g.new_kind == JOKER ? 4 : 2) : 0);
    panel_open(h);
    panel_line(1, INTRO_TITLE, g.shift);
    panel_line(2, intro_goal[g.goal], g.target);
    if (g.goal == GOAL_KIND) {
        put_kind(1, 9, 3, g.goal_kind);
        y++;
    }
    if (g.shift == 1) {
        print(1, 1, y, HELP, 0);
    } else if (g.new_kind) {
        panel_line(y, INTRO_NEW, 0);
        put_kind(1, 9, y + 1, g.new_kind);
        if (g.new_kind == JOKER)
            panel_line(y + 2, INTRO_JOKER, 0);
    }
    panel_line(h - 2, START_HINT, 0);
}

/* the panel for the current phase (after the pause, too) */
static void phase_panel(void)
{
    switch (g.phase) {
    case PHASE_INTRO:
        intro_panel();
        break;
    case PHASE_DONE:
        panel_open(g.repaired ? 11 : 10);
        panel_line(1, DONE_TITLE, g.shift);
        panel_value(3, DONE_EMPTY, g.bonus_empty / 50, g.bonus_empty);
        panel_value(4, DONE_SHIFT, g.shift, g.bonus_shift);
        if (g.repaired)
            panel_line(5, DONE_REPAIR, 0);
        print(1, 1, g.repaired ? 7 : 6, DONE_TOTAL, 0);
        print_num(1, 11, g.repaired ? 7 : 6, g.score > 9999999UL ? 9999999UL : g.score, 7);
        panel_line(g.repaired ? 9 : 8, CONTINUE, 0);
        break;
    case PHASE_OVER:
        panel_open(8);
        panel_line(1, OVER_TITLE, 0);
        panel_line(2, OVER_WHY, 0);
        panel_line(4, OVER_SCORE, g.score);
        panel_line(5, OVER_SHIFT, g.shift);
        break;
    default:
        panel_close();
        return;
    }
    panel_show();
}

static void sounds(void)
{
    uint8_t i;
    Event *e;

    for (i = 0; i < g.nevents; i++) {
        e = &g.events[i];
        switch (e->type) {
        case EV_MOVE: sfx_play(SFX_MOVE); break;
        case EV_SPAWN: sfx_play(SFX_SPAWN); break;
        case EV_CATCH: sfx_play(SFX_CATCH); break;
        case EV_DROP: sfx_play(SFX_DROP); break;
        case EV_LAND: sfx_play(SFX_LAND); break;
        case EV_TOSS: sfx_play(SFX_TOSS); break;
        case EV_BREAK: sfx_play(SFX_BREAK); break;
        case EV_WARN: sfx_play(SFX_WARN); break;
        case EV_DENY: sfx_play(SFX_DENY); break;
        case EV_LINES: sfx_play(e->chained ? SFX_CHAIN : e->big ? SFX_LINE_BIG : SFX_LINE); break;
        case EV_START: sfx_play(SFX_SHIFT_START); break;
        case EV_DONE: sfx_play(SFX_SHIFT_DONE); break;
        case EV_OVER:
            music_stop();
            sfx_play(SFX_GAME_OVER);
            break;
        }
    }
}

/* the pause; returns 1 if the game is to end */
static uint8_t pause(void)
{
    music_stop();
    sfx_play(SFX_PAUSE);
    render_hide_sprites();
    panel_open(6);
    panel_line(1, PAUSE_TITLE, 0);
    panel_line(3, PAUSE_RESUME, 0);
    panel_line(4, PAUSE_QUIT, 0);
    panel_show();
    for (;;) {
        frame();
        if (pressed & J_SELECT) {
            panel_close();
            return 1;
        }
        if (pressed & (J_START | J_A))
            break;
    }
    sfx_play(SFX_PAUSE);
    music_resume();
    phase_panel();
    return 0;
}

/* the music menu: up and down play the songs, A (or any other button) closes it */
static void music_menu(void)
{
    uint8_t i, n = music_song;

    panel_open(SONGS + 6);
    panel_line(1, MUSIC_TITLE, 0);
    for (i = 0; i < SONGS; i++)
        print(1, 3, 3 + i, SONG_0 + i, 0);
    panel_line(SONGS + 4, MUSIC_HELP, 0);
    vset(1, 1, 3 + n, CH('>'));
    panel_show();
    for (;;) {
        frame();
        if (pressed & (J_A | J_B | J_START | J_SELECT))
            break;
        if (pressed & (J_UP | J_DOWN)) {
            vset(1, 1, 3 + n, T_SPACE);
            if (pressed & J_UP)
                n = n ? n - 1 : SONGS - 1;
            else
                n = n + 1 == SONGS ? 0 : n + 1;
            vset(1, 1, 3 + n, CH('>'));
            music_select(n);
        }
    }
    save_store();
    phase_panel();
}

/* one game tick with its sounds, panels and effects */
static void tick(const Input *in)
{
    uint8_t phase = g.phase, shift = g.shift;

    game_tick(in);
    sounds();
    if (g.shift != shift)
        render_field();
    if (g.phase != phase || g.shift != shift)
        phase_panel();
    render_events();
}

static void play(void)
{
    uint8_t held = 0, missed;
    Input in;
    static const Input none;

    game_new((uint16_t)(frames * 40503U) ^ ((uint16_t)DIV_REG << 8) ^ 0x5a5a);
    render_field();
    phase_panel();
    music_start();
    last_vbl = vbl_count;
    for (;;) {
        frame();
        /* frames missed while the last one took too long (a cascade of lines, say) are
           caught up with an extra tick, so the game keeps its speed */
        missed = vbl_count - last_vbl - 1;
        last_vbl = vbl_count;
        if ((pressed & J_START) && g.phase <= PHASE_PLAY) {
            if (pause())
                break;
            last_vbl = vbl_count;
            continue;
        }
        if ((pressed & J_SELECT) && g.phase <= PHASE_PLAY) {
            music_menu();
            last_vbl = vbl_count;
            continue;
        }
        /* left and right repeat while held */
        in.move = 0;
        if (keys & (J_LEFT | J_RIGHT)) {
            if (pressed & (J_LEFT | J_RIGHT))
                held = 0;
            if (held == 0 || (held >= REPEAT_DELAY && (held - REPEAT_DELAY) % REPEAT_EVERY == 0))
                in.move = keys & J_LEFT ? -1 : 1;
            if (held < 255)
                held++;
        }
        in.drop = (pressed & J_A) != 0;
        in.toss = (pressed & (J_B | J_UP)) != 0;
        in.fast = (keys & J_DOWN) != 0;
        tick(&in);
        if (missed && !g.done) {
            sound_tick();
            tick(&none);
        }
        render_frame();
        if (g.done)
            break;
    }
    music_stop();
}

/* ---------------------------------------------------------------- name entry */

static const uint8_t charset[] = {
    CH(' '), CH('A'), CH('B'), CH('C'), CH('D'), CH('E'), CH('F'), CH('G'), CH('H'), CH('I'), CH('J'), CH('K'),
    CH('L'), CH('M'), CH('N'), CH('O'), CH('P'), CH('Q'), CH('R'), CH('S'), CH('T'), CH('U'), CH('V'), CH('W'),
    CH('X'), CH('Y'), CH('Z'), T_AE, T_OE, T_UE, CH('0'), CH('1'), CH('2'), CH('3'), CH('4'), CH('5'), CH('6'),
    CH('7'), CH('8'), CH('9'), CH('-'), CH('.'), CH('!'),
};

static uint8_t char_index(uint8_t t)
{
    uint8_t i;
    for (i = 0; i < sizeof(charset); i++)
        if (charset[i] == t)
            return i;
    return 0;
}

#define NAME_X 6
#define NAME_Y 9

static void entry(uint8_t rank, uint32_t score, uint8_t shift)
{
    uint8_t name[NAME_LEN], pos = 0, i, k, held = 0, empty;

    memcpy(name, last_name, NAME_LEN);
    render_clear();
    print_center(0, 2, ENTRY_TITLE, 0);
    print_center(0, 4, ENTRY_RANK, rank + 1);
    print_center(0, 5, ENTRY_POINTS, score);
    print_center(0, 7, ENTRY_PROMPT, 0);
    print_center(0, 13, ENTRY_HELP, 0);
    for (;;) {
        for (i = 0; i < NAME_LEN; i++) {
            vset(0, NAME_X + i, NAME_Y, name[i]);
            vset(0, NAME_X + i, NAME_Y + 1, i == pos && (frames & 16) ? T_CURSOR : T_SPACE);
        }
        frame();
        if (pressed & J_START)
            break;
        k = char_index(name[pos]);
        /* up and down repeat while held */
        if (keys & (J_UP | J_DOWN)) {
            if (pressed & (J_UP | J_DOWN))
                held = 0;
            if (held == 0 || (held >= 20 && (held & 3) == 0)) {
                if (keys & J_UP)
                    k = k + 1 == sizeof(charset) ? 0 : k + 1;
                else
                    k = k ? k - 1 : sizeof(charset) - 1;
                name[pos] = charset[k];
                sfx_play(SFX_KEY);
            }
            if (held < 255)
                held++;
        }
        if ((pressed & (J_RIGHT | J_A)) && pos < NAME_LEN - 1) {
            pos++;
            sfx_play(SFX_KEY);
        }
        if ((pressed & (J_LEFT | J_B)) && pos > 0) {
            pos--;
            sfx_play(SFX_KEY);
        }
    }
    empty = 1;
    for (i = 0; i < NAME_LEN; i++)
        if (name[i] != T_SPACE)
            empty = 0;
    if (empty) {
        name[0] = name[1] = name[2] = CH('?');
    } else {
        memcpy(last_name, name, NAME_LEN);
    }
    sfx_play(SFX_SHIFT_DONE);
    score_insert(rank, name, score, shift);
}

void main(void)
{
    uint8_t rank;

    render_init();
    sound_init();
    save_load();
    for (;;) {
        title();
        play();
        last_score = g.score;
        last_played = 1;
        rank = score_rank(g.score);
        last_record = rank == 0;
        if (rank < SCORES)
            entry(rank, g.score, g.shift);
    }
}
