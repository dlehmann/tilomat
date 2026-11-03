/*
 * test_game.c - tests of the game logic on the PC (make test): the lines in the bin, the
 * belt, the cart, and whole games played by a simple player.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"

static int failed;

#define CHECK(cond) do { if (!(cond)) { printf("%s:%d: %s\n", __FILE__, __LINE__, #cond); failed++; } } while (0)

static Input none;

static void tick(const Input *in)
{
    game_tick(in);
}

/* plays the intro away */
static void to_play(void)
{
    Input a = {0, 1, 0, 0};
    int i;

    for (i = 0; i < 40 && g.phase != PHASE_PLAY; i++)
        tick(i >= 31 ? &a : &none);
}

/* sets a bin from rows of characters (top row first), '.' empty, digits kinds, 'J' joker */
static void set_bin(const char *rows[BIN_ROWS])
{
    int c, r;

    for (r = 0; r < BIN_ROWS; r++)
        for (c = 0; c < LANES; c++) {
            char ch = rows[BIN_ROWS - 1 - r][c];
            g.bin[CELL(c, r)] = ch == '.' ? 0 : ch == 'J' ? JOKER : ch - '0';
        }
    memset(g.fall, 0, sizeof(g.fall));
    g.need_sweep = 1;
}

/* the events of type t in the last tick */
static Event *find(int t)
{
    int i;

    for (i = 0; i < g.nevents; i++)
        if (g.events[i].type == t)
            return &g.events[i];
    return NULL;
}

static void test_lines(void)
{
    const char *across[] = {".....", ".....", ".....", ".....", "111.2"};
    const char *joker[] = {".....", ".....", ".....", ".....", "11J22"};
    const char *diag[] = {".....", ".....", "..3..", ".32..", "312.."};
    const char *none_[] = {".....", ".....", ".....", "12121", "21212"};
    Event *e;

    game_new(1);
    to_play();
    set_bin(across);
    tick(&none);
    e = find(EV_LINES);
    CHECK(e && e->count == 1 && e->points == 200 && e->mult == 1);
    CHECK(g.clearing);
    while (g.clearing)
        tick(&none);
    CHECK(g.bin[CELL(0, 0)] == 0 && g.bin[CELL(4, 0)] == 2);

    game_new(1);
    to_play();
    set_bin(joker);
    tick(&none);
    e = find(EV_LINES);
    CHECK(e && e->count == 2 && e->points == 400);

    game_new(1);
    to_play();
    set_bin(diag);
    tick(&none);
    e = find(EV_LINES);
    CHECK(e && e->count == 1 && e->points == 300);

    game_new(1);
    to_play();
    set_bin(none_);
    tick(&none);
    CHECK(!find(EV_LINES));
}

/* a cascade: clearing the 2s lets the 1 above fall into a line of 1s, at twice the points */
static void test_cascade(void)
{
    const char *rows[] = {".....", ".....", "..1..", "11222", "33133"};
    Event *e;
    int i, second = 0;

    game_new(1);
    to_play();
    set_bin(rows);
    tick(&none);
    e = find(EV_LINES);
    CHECK(e && e->mult == 1);
    for (i = 0; i < 120 && !second; i++) {
        tick(&none);
        e = find(EV_LINES);
        if (e)
            second = e->mult;
    }
    CHECK(second == 2);
}

static void test_cart(void)
{
    int i;
    Input left = {-1, 0, 0, 0}, drop = {0, 1, 0, 0}, toss = {0, 0, 1, 0};

    game_new(7);
    to_play();
    CHECK(g.cart == 2);
    for (i = 0; i < 5; i++)
        tick(&left);
    CHECK(g.cart == 0);
    tick(&drop);
    CHECK(find(EV_DENY) != NULL);
    g.stack[0] = 1;
    g.stack[1] = 2;
    g.stack_n = 2;
    tick(&toss);
    CHECK(g.stack_n == 1 && KIND(g.belt[0][TOSS_SLOT]) == 2);
    tick(&drop);
    CHECK(g.stack_n == 0 && g.bin[CELL(0, 0)] == 1 && g.fall[CELL(0, 0)] > 0);
}

/* the cart catches a tile as soon as it touches the top of the stack */
static void test_catch(void)
{
    Input none = {0, 0, 0, 0};
    uint8_t *lane;

    game_new(7);
    to_play();
    memset(g.belt, 0, sizeof(g.belt));
    lane = g.belt[g.cart];
    g.stack[0] = 1;
    g.stack[1] = 2;
    g.stack_n = 2;
    lane[2] = 3;                        /* half way to slot 1: still above the stack */
    g.step_pos = g.step_ticks / 2;
    tick(&none);
    CHECK(g.half && g.stack_n == 2 && lane[2] == 3);
    lane[1] = lane[2];                  /* in slot 1 it touches the stack */
    lane[2] = 0;
    g.step_pos = 0;
    tick(&none);
    CHECK(g.stack_n == 3 && !lane[1] && KIND(g.stack[2]) == 3 && find(EV_CATCH));
    g.stack_n = 0;                      /* the empty cart: in slot 0 */
    lane[1] = 4;
    g.step_pos = g.step_ticks / 2;
    tick(&none);
    CHECK(g.stack_n == 0 && lane[1] == 4);
    lane[0] = lane[1];
    lane[1] = 0;
    g.step_pos = 0;
    tick(&none);
    CHECK(g.stack_n == 1 && !lane[0]);
}

/* a simple player: waits under the nearest tile, drops onto a column where it fits best */
static int score_col(int col, int kind)
{
    int h = 0, s = 0, dc, dr;

    while (h < BIN_ROWS && g.bin[CELL(col, h)])
        h++;
    if (h >= BIN_ROWS)
        return -100;
    for (dc = -1; dc <= 1; dc++)
        for (dr = -1; dr <= 1; dr++) {
            int c = col + dc, r = h + dr;
            if ((dc || dr) && c >= 0 && c < LANES && r >= 0 && r < BIN_ROWS) {
                int v = g.bin[CELL(c, r)];
                if (v == kind || v == JOKER || kind == JOKER)
                    s += 4;
            }
        }
    return s - h;
}

static void play(Input *in, int mistakes)
{
    int lane, s, best = -1, bests = 99, kind, target, c, v, bv = -1000;

    memset(in, 0, sizeof(*in));
    if (g.phase != PHASE_PLAY) {
        in->drop = g.timer % 40 == 39;
        return;
    }
    if (rand() % 1000 < mistakes)
        return;
    for (lane = 0; lane < LANES; lane++)
        for (s = 0; s < SLOTS; s++)
            if (g.belt[lane][s] && s < bests) {
                bests = s;
                best = lane;
            }
    if (g.stack_n && (g.stack_n == CART_MAX || bests > 3 || best < 0)) {
        kind = KIND(g.stack[g.stack_n - 1]);
        target = g.cart;
        for (c = 0; c < LANES; c++) {
            v = score_col(c, kind) - (c > g.cart ? c - g.cart : g.cart - c);
            if (v > bv) {
                bv = v;
                target = c;
            }
        }
        if (bv <= -90) {
            in->toss = g.timer % 8 == 0;
            return;
        }
        if (target != g.cart)
            in->move = target < g.cart ? -1 : 1;
        else
            in->drop = g.timer % 4 == 0;
        if (g.timer % 3)
            in->move = 0;
        return;
    }
    if (best >= 0 && best != g.cart && g.timer % 3 == 0)
        in->move = best < g.cart ? -1 : 1;
}

static void test_games(void)
{
    int run, t;
    Input in;

    for (run = 0; run < 6; run++) {
        int maxshift = 0;
        srand(run);
        game_new((uint16_t)(run * 7919 + 1));
        for (t = 0; t < 60 * 60 * 30 && !g.done; t++) {
            play(&in, run * 150);
            tick(&in);
            CHECK(g.stack_n <= CART_MAX);
            if (g.shift > maxshift)
                maxshift = g.shift;
        }
        printf("game %d (mistakes %d/1000): shift %d, score %lu, %s after %d s\n", run, run * 150, maxshift,
               (unsigned long)g.score, g.done ? "over" : "running", t / 60);
    }
}

int main(void)
{
    test_lines();
    test_cascade();
    test_cart();
    test_catch();
    test_games();
    if (failed) {
        printf("%d checks failed\n", failed);
        return 1;
    }
    printf("all checks passed\n");
    return 0;
}
