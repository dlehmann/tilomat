/*
 * game.c - the game logic of Tilomat, see game.h.
 *
 * Tiles come out of the machine and slide down a belt of five lanes. The cart at the near end
 * catches them (up to four) and drops them into the bin, five columns of five cells. Three or
 * more tiles of one kind in a line - across, up or diagonal; the joker fits every kind - are
 * cleared, the tiles above fall down and may form further lines: each round of such a cascade
 * multiplies the points by one more. A tile that falls off the belt breaks; the fourth break
 * ends the game, and every finished shift replaces one.
 *
 * Every shift has an order (lines, tiles, lines of one kind, points, long lines). It ends
 * when the order is filled; free cells in the bin give a bonus, so a tidy bin pays.
 */
#include "game.h"

#include <string.h>

#define CLEAR_TICKS  36     /* the flash of cleared lines */
#define FAST         4      /* belt speed while down is held */
#define JOKER_CHANCE 16     /* one tile in JOKER_CHANCE is a joker */
#define INTRO_TICKS  (TICK_HZ * 4)
#define INTRO_FIRST  (TICK_HZ * 20)
#define DONE_TICKS   (TICK_HZ * 6)
#define OVER_TICKS   (TICK_HZ * 8)
#define SKIP_TICKS   (TICK_HZ / 2)

Game g;

/* ---------------------------------------------------------------- shifts */

static const uint8_t goal_order[5] = {GOAL_LINES, GOAL_TILES, GOAL_KIND, GOAL_POINTS, GOAL_BIG};

uint8_t shift_goal(uint8_t shift)
{
    return goal_order[(shift - 1) % 5];
}

static uint16_t cap(uint16_t v, uint16_t max)
{
    return v > max ? max : v;
}

/* the size of the order: a little more with every block of five shifts */
uint16_t shift_target(uint8_t shift)
{
    uint8_t b = (shift - 1) / 5;

    switch (shift_goal(shift)) {
    case GOAL_LINES:  return cap(3 + 2 * b, 15);
    case GOAL_TILES:  return cap(20 + 8 * b, 80);
    case GOAL_KIND:   return cap(2 + b, 8);
    case GOAL_POINTS: return cap(2 + 2 * b, 30) * 1000;
    default:          return cap(1 + b / 2, 5);
    }
}

/* 3 kinds at first, 4 from shift 3, 5 from 6, 6 from 12 */
static uint8_t shift_kinds(uint8_t shift)
{
    if (shift >= 12)
        return 6;
    if (shift >= 6)
        return 5;
    if (shift >= 3)
        return 4;
    return 3;
}

#define JOKER_SHIFT 8

/* ticks per belt slot: 33 in the first shift, one less with every shift, 12 at the fastest */
static uint8_t shift_step(uint8_t shift)
{
    return shift >= 22 ? 12 : 34 - shift;
}

uint16_t intro_ticks(void)
{
    return g.shift == 1 ? INTRO_FIRST : INTRO_TICKS;
}

/* ---------------------------------------------------------------- lines */

/* every line of the bin that can hold three: 5 rows, 5 columns, 5 + 5 diagonals */
#define LINES 20
static uint8_t line_cells[LINES][5];
static uint8_t line_len[LINES];
static uint8_t line_dir[LINES];

static void init_lines(void)
{
    uint8_t n = 0, i, c, r;
    int8_t d, col;

    for (r = 0; r < BIN_ROWS; r++, n++) {
        for (c = 0; c < LANES; c++)
            line_cells[n][c] = CELL(c, r);
        line_len[n] = LANES;
        line_dir[n] = DIR_HORZ;
    }
    for (c = 0; c < LANES; c++, n++) {
        for (r = 0; r < BIN_ROWS; r++)
            line_cells[n][r] = CELL(c, r);
        line_len[n] = BIN_ROWS;
        line_dir[n] = DIR_VERT;
    }
    /* up to the right (col = row + d) and up to the left (col = d - row) */
    for (i = 0; i < 2; i++) {
        for (d = i ? 2 : -2; i ? d <= 6 : d <= 2; d++, n++) {
            line_len[n] = 0;
            for (r = 0; r < BIN_ROWS; r++) {
                col = i ? d - (int8_t)r : (int8_t)r + d;
                if (col >= 0 && col < LANES)
                    line_cells[n][line_len[n]++] = CELL(col, r);
            }
            line_dir[n] = DIR_DIAG;
        }
    }
}

/* the points of a line: by its length, times 2 across and 3 diagonal */
static const uint16_t line_points[3] = {100, 400, 1000};
static const uint8_t dir_factor[3] = {2, 1, 3};

/* what one sweep found */
static uint8_t sw_lines, sw_kind, sw_big, sw_cell;
static uint16_t sw_points;

/*
 * The lines on one line of cells: a run of tiles whose tiles other than jokers are all of
 * one kind. Only the longest runs count, so a joker can belong to two lines of different
 * kinds.
 */
static uint8_t sl_i, sl_j, sl_n, sl_v, sl_kind, sl_end, sl_len;
static const uint8_t *sl_cells;

static void sweep_line(uint8_t k)
{
    sl_cells = line_cells[k];
    /* every run of three on a line of up to five cells covers its third cell */
    if (!g.bin[sl_cells[2]])
        return;
    sl_n = line_len[k];
    sl_end = 0;
    for (sl_i = 0; sl_i + 3 <= sl_n; sl_i++) {
        sl_kind = 0;
        for (sl_j = sl_i; sl_j < sl_n; sl_j++) {
            sl_v = g.bin[sl_cells[sl_j]];
            if (!sl_v)
                break;
            if (sl_v == JOKER)
                continue;
            if (!sl_kind)
                sl_kind = sl_v;
            else if (sl_v != sl_kind)
                break;
        }
        sl_len = sl_j - sl_i;
        if (sl_len < 3 || sl_j <= sl_end)
            continue;
        sl_end = sl_j;
        sw_lines++;
        if (sl_len >= 4)
            sw_big++;
        if (!sl_kind || sl_kind == g.goal_kind)
            sw_kind++;
        sw_points += line_points[sl_len - 3] * dir_factor[line_dir[k]];
        sw_cell = sl_cells[sl_i];
        for (sl_v = sl_i; sl_v < sl_j; sl_v++)
            g.marked[sl_cells[sl_v]] = 1;
    }
}

/* ---------------------------------------------------------------- the game */

static uint16_t rnd(void)
{
    uint16_t x = g.rng;

    x ^= x << 7;
    x ^= x >> 9;
    x ^= x << 8;
    g.rng = x;
    return x;
}

/* a random number 0..n-1, without a division (slow on the Game Boy) */
static uint8_t rnd_n(uint8_t n)
{
    return ((uint16_t)(uint8_t)rnd() * n) >> 8;
}

static Event *emit(uint8_t type, uint8_t lane, uint8_t row, uint8_t kind)
{
    Event *e;

    if (g.nevents >= MAX_EVENTS)
        return 0;
    e = &g.events[g.nevents++];
    memset(e, 0, sizeof(Event));
    e->type = type;
    e->lane = lane;
    e->row = row;
    e->kind = kind;
    return e;
}

static void add_points(uint32_t p)
{
    g.score += p;
    g.shift_points += p;
}

/* the next kind: a shuffled bag holding every kind twice keeps them even; the joker by chance */
static uint8_t next_kind(void)
{
    uint8_t i, j, t;

    if (g.joker && (rnd() & (JOKER_CHANCE - 1)) == 0)
        return JOKER;
    if (g.bag_n == 0) {
        for (i = 0; i < g.kinds; i++)
            g.bag[2 * i] = g.bag[2 * i + 1] = i + 1;
        g.bag_n = 2 * g.kinds;
        for (i = g.bag_n - 1; i > 0; i--) {
            j = rnd_n(i + 1);
            t = g.bag[i];
            g.bag[i] = g.bag[j];
            g.bag[j] = t;
        }
    }
    return g.bag[--g.bag_n];
}

/* belt steps until the next tile: wide gaps at first, closer later */
static uint8_t spawn_gap(void)
{
    if (g.shift <= 5)
        return 3;
    if (g.shift <= 15)
        return 2 + rnd_n(2);
    return 1 + rnd_n(3);
}

static void start_shift(uint8_t shift)
{
    uint8_t kinds = shift_kinds(shift);

    g.shift = shift;
    g.phase = PHASE_INTRO;
    g.timer = 0;
    g.goal = shift_goal(shift);
    g.target = shift_target(shift);
    g.count = 0;
    g.shift_points = 0;
    g.new_kind = shift > 1 && kinds > shift_kinds(shift - 1) ? kinds : shift == JOKER_SHIFT ? JOKER : 0;
    g.kinds = kinds;
    g.joker = shift >= JOKER_SHIFT;
    g.goal_kind = 1 + rnd_n(kinds);
    g.step_ticks = shift_step(shift);
    g.step_pos = 0;
    g.half = 0;
    g.spawn_wait = 1;
    g.last_lane = 0xff;
    g.bag_n = 0;
    memset(g.belt, 0, sizeof(g.belt));
    g.stack_n = 0;
    g.cart = LANES / 2;
    memset(g.bin, 0, sizeof(g.bin));
    memset(g.marked, 0, sizeof(g.marked));
    memset(g.fall, 0, sizeof(g.fall));
    g.clearing = 0;
    g.chain = 0;
    g.need_sweep = 0;
    g.belt_dirty = g.bin_dirty = ALL_COLUMNS;
    emit(EV_START, 0, 0, 0);
}

void game_new(uint16_t seed)
{
    memset(&g, 0, sizeof(g));
    init_lines();
    g.rng = seed ? seed : 0x7117;
    start_shift(1);
}

static void broken(uint8_t lane, uint8_t kind)
{
    g.breaks++;
    emit(EV_BREAK, lane, 0, kind);
    if (g.breaks >= BREAKS) {
        g.phase = PHASE_OVER;
        g.timer = 0;
        emit(EV_OVER, 0, 0, 0);
    } else if (g.breaks == BREAKS - 1) {
        emit(EV_WARN, 0, 0, 0);
    }
}

/* a new tile at the far end, in a lane whose next slot is free (so tiles never touch) */
static void spawn(void)
{
    uint8_t lane = rnd_n(LANES), k;

    if (lane == g.last_lane)
        lane = rnd_n(LANES);
    for (k = 0; k < LANES; k++) {
        if (!g.belt[lane][SLOTS - 2])
            break;
        lane = lane == LANES - 1 ? 0 : lane + 1;
    }
    if (k == LANES) {
        g.spawn_wait = 1;
        return;
    }
    g.belt[lane][SLOTS - 1] = next_kind();
    g.last_lane = lane;
    g.spawn_wait = spawn_gap();
    emit(EV_SPAWN, lane, 0, KIND(g.belt[lane][SLOTS - 1]));
}

/* the belt moves on by a slot: the tiles at the near end go onto the cart or break */
static void belt_step(void)
{
    uint8_t lane, s, t;

    for (lane = 0; lane < LANES; lane++) {
        t = g.belt[lane][0];
        if (!t)
            continue;
        if (lane == g.cart && g.stack_n < CART_MAX) {
            if (!(t & COUNTED)) {
                t |= COUNTED;
                if (g.goal == GOAL_TILES)
                    g.count++;
            }
            g.stack[g.stack_n++] = t;
            add_points(POINTS_CATCH);
            emit(EV_CATCH, lane, 0, KIND(t));
        } else {
            broken(lane, KIND(t));
        }
    }
    for (lane = 0; lane < LANES; lane++) {
        for (s = 0; s < SLOTS - 1; s++)
            g.belt[lane][s] = g.belt[lane][s + 1];
        g.belt[lane][SLOTS - 1] = 0;
    }
    g.steps++;
    g.belt_dirty = 1;
    if (g.phase == PHASE_PLAY && --g.spawn_wait == 0)
        spawn();
}

static uint8_t column_height(uint8_t col)
{
    uint8_t h = 0;
    const uint8_t *b = &g.bin[CELL(col, 0)];

    while (h < BIN_ROWS && b[h])
        h++;
    return h;
}

static void drop_tile(void)
{
    uint8_t col = g.cart, row = column_height(col), c;

    if (!g.stack_n || g.clearing || row >= BIN_ROWS) {
        emit(EV_DENY, col, row, 0);
        return;
    }
    c = CELL(col, row);
    g.bin[c] = KIND(g.stack[--g.stack_n]);
    /* falls from the rim, a row above the bin, into its cell */
    g.fall[c] = (BIN_ROWS - row) * 2;
    g.need_sweep = 1;
    g.bin_dirty |= 1 << col;
    emit(EV_DROP, col, row, g.bin[c]);
}

/* back onto the belt, to the nearest slot with free slots on both sides */
static void toss_tile(void)
{
    uint8_t *lane = g.belt[g.cart], z;

    if (g.stack_n)
        for (z = TOSS_SLOT; z < SLOTS - 1; z++)
            if (!lane[z - 1] && !lane[z] && !lane[z + 1]) {
                lane[z] = g.stack[--g.stack_n];
                g.belt_dirty = 1;
                emit(EV_TOSS, g.cart, z, KIND(lane[z]));
                return;
            }
    emit(EV_DENY, g.cart, 0, 0);
}

/* the falling tiles move half a row; returns 1 if all have landed */
static uint8_t settle(void)
{
    uint8_t col, r, still = 0, landed = 0, *f = g.fall;

    for (col = 0; col < LANES; col++)
        for (r = 0; r < BIN_ROWS; r++, f++) {
            if (!*f)
                continue;
            g.bin_dirty |= 1 << col;
            if (--*f)
                still = 1;
            else
                landed = col + 1;
        }
    if (landed)
        emit(EV_LAND, landed - 1, 0, 0);
    return !still;
}

static void sweep(void)
{
    uint8_t k;
    uint16_t sum;
    Event *e;

    g.need_sweep = 0;
    memset(g.marked, 0, sizeof(g.marked));
    sw_lines = sw_kind = sw_big = 0;
    sw_points = 0;
    for (k = 0; k < LINES; k++)
        sweep_line(k);
    if (!sw_lines) {
        g.chain = 0;
        return;
    }
    g.chain++;
    g.clearing = CLEAR_TICKS;
    g.bin_dirty = ALL_COLUMNS;
    sum = 0;
    for (k = 0; k < g.chain; k++)
        sum += sw_points;
    add_points(sum);

    switch (g.goal) {
    case GOAL_LINES: g.count += sw_lines; break;
    case GOAL_KIND:  g.count += sw_kind; break;
    case GOAL_BIG:   g.count += sw_big; break;
    }

    e = emit(EV_LINES, sw_cell / BIN_ROWS, sw_cell % BIN_ROWS, 0);
    if (!e)
        return;
    e->points = sum;
    e->count = sw_lines;
    e->mult = g.chain;
    e->big = sw_big || sw_lines > 1;
    e->chained = g.chain > 1;
}

/* the cleared tiles go, the tiles above fall down */
static void clear_marked(void)
{
    uint8_t col, r, to, *b, *m, *f;

    for (col = 0; col < LANES; col++) {
        b = &g.bin[CELL(col, 0)];
        m = &g.marked[CELL(col, 0)];
        f = &g.fall[CELL(col, 0)];
        to = 0;
        for (r = 0; r < BIN_ROWS; r++) {
            if (m[r] || !b[r])
                continue;
            if (to != r) {
                b[to] = b[r];
                f[to] = f[r] + (r - to) * 2;
            }
            to++;
        }
        for (; to < BIN_ROWS; to++)
            b[to] = f[to] = 0;
    }
    memset(g.marked, 0, sizeof(g.marked));
    g.need_sweep = 1;
    g.bin_dirty = ALL_COLUMNS;
}

static uint8_t goal_reached(void)
{
    if (g.goal == GOAL_POINTS)
        return g.shift_points >= g.target;
    return g.count >= g.target;
}

static void finish_shift(void)
{
    uint8_t c, empty = 0;

    for (c = 0; c < CELLS; c++)
        if (!g.bin[c])
            empty++;
    g.bonus_empty = empty * 50;
    g.bonus_shift = g.shift * 250;
    g.score += g.bonus_empty + g.bonus_shift;
    g.repaired = g.breaks > 0;
    if (g.repaired)
        g.breaks--;
    g.phase = PHASE_DONE;
    g.timer = 0;
    emit(EV_DONE, 0, 0, 0);
}

static void tick_play(const Input *in)
{
    uint8_t settled, half;

    if (in->move < 0 && g.cart > 0) {
        g.cart--;
        emit(EV_MOVE, g.cart, 0, 0);
    } else if (in->move > 0 && g.cart < LANES - 1) {
        g.cart++;
        emit(EV_MOVE, g.cart, 0, 0);
    }
    if (in->drop)
        drop_tile();
    if (in->toss)
        toss_tile();

    settled = settle();
    if (g.clearing && --g.clearing == 0)
        clear_marked();
    else if (g.clearing && (g.clearing & 3) == 0)
        g.bin_dirty = ALL_COLUMNS;        /* the flash */
    if (g.need_sweep && !g.clearing && settled)
        sweep();

    g.step_pos += in->fast ? FAST : 1;
    while (g.step_pos >= g.step_ticks && g.phase == PHASE_PLAY) {
        g.step_pos -= g.step_ticks;
        belt_step();
    }
    if (g.phase != PHASE_PLAY)
        return;
    half = g.step_pos * 2 >= g.step_ticks;
    if (half != g.half) {
        g.half = half;
        g.belt_dirty = 1;
    }
    if (!g.clearing && !g.need_sweep && settled && goal_reached())
        finish_shift();
}

void game_tick(const Input *in)
{
    uint8_t go;

    g.nevents = 0;
    g.timer++;
    go = in->drop && g.timer >= SKIP_TICKS;

    switch (g.phase) {
    case PHASE_INTRO:
        if (go || g.timer >= intro_ticks()) {
            g.phase = PHASE_PLAY;
            g.timer = 0;
        }
        break;
    case PHASE_PLAY:
        tick_play(in);
        break;
    case PHASE_DONE:
        if (g.timer >= DONE_TICKS || (in->drop && g.timer >= TICK_HZ))
            start_shift(g.shift + 1);
        break;
    case PHASE_OVER:
        if (g.timer >= OVER_TICKS || (in->drop && g.timer >= TICK_HZ * 3 / 2))
            g.done = 1;
        break;
    }
}
