/*
 * game.h - the game logic of Tilomat: belt, cart, bin, lines, shifts and points.
 *
 * Without any Game Boy code, so it also compiles on the PC (tools/test_game.c). The logic runs
 * one tick per frame; game_tick() takes the input of one tick and reports what happened as
 * events (sounds and effects of the front end).
 */
#ifndef GAME_H
#define GAME_H

#include <stdint.h>

#define TICK_HZ     60
#define LANES       5       /* belt lanes = bin columns */
#define SLOTS       9       /* tile positions on the belt, slot 0 at the near end */
#define BIN_ROWS    5
#define CELLS       (LANES * BIN_ROWS)
#define ALL_COLUMNS ((1 << LANES) - 1)
#define CART_MAX    4       /* tiles the cart can hold */
#define BREAKS      4       /* the fourth broken tile ends the game */
#define KINDS_MAX   6
#define JOKER       7       /* the joker tile, fits every kind */
#define TOSS_SLOT   2       /* the nearest belt slot a tossed tile lands on */
#define MAX_EVENTS  12

#define POINTS_CATCH 10

/* the bin is one array, cell = column * BIN_ROWS + row, row 0 at the bottom */
#define CELL(col, row) ((col) * BIN_ROWS + (row))

enum { GOAL_LINES, GOAL_TILES, GOAL_KIND, GOAL_POINTS, GOAL_BIG };

/* directions of a line */
enum { DIR_HORZ, DIR_VERT, DIR_DIAG };

enum {
    PHASE_INTRO,            /* the order of the shift (first shift: and the controls) */
    PHASE_PLAY,
    PHASE_DONE,             /* shift done, bonus */
    PHASE_OVER,             /* the break meter is full */
};

enum {
    EV_MOVE,                /* cart moved */
    EV_SPAWN,               /* a tile came out of the machine: lane */
    EV_CATCH,               /* tile caught: lane, kind */
    EV_DROP,                /* tile dropped into the bin: lane, row */
    EV_LAND,                /* a falling tile landed: lane */
    EV_TOSS,                /* tile tossed back onto the belt: lane */
    EV_BREAK,               /* tile fell off the belt and broke: lane, kind */
    EV_WARN,                /* one more break ends the game */
    EV_DENY,                /* drop or toss not possible: lane */
    EV_LINES,               /* lines cleared: points, count, mult, big, lane/row of a cell */
    EV_START,
    EV_DONE,
    EV_OVER,
};

typedef struct {
    uint8_t type, lane, row, kind;
    uint8_t count;          /* lines */
    uint8_t mult;           /* the points were multiplied by this (cascade) */
    uint8_t big;            /* a line of 4 or 5, or several at once */
    uint8_t chained;        /* follows lines of the same cascade */
    uint16_t points;
} Event;

/* the input of one tick */
typedef struct {
    int8_t move;            /* -1 left, +1 right (key repeat is done by the caller) */
    uint8_t drop;           /* A pressed: drop a tile; continues on the panels */
    uint8_t toss;           /* B or up pressed: toss a tile back */
    uint8_t fast;           /* down held: the belt runs faster */
} Input;

/* a tile on the belt or the cart: the kind, COUNTED once caught for a tile order */
#define COUNTED 0x80
#define KIND(t) ((t) & 0x0f)

typedef struct {
    uint16_t rng;
    uint8_t phase;
    uint16_t timer;         /* ticks in the current phase */
    uint8_t done;           /* the game is over and the end panel has been seen */

    uint8_t shift;          /* 1.. */
    uint32_t score;
    uint32_t shift_points;
    uint8_t goal;
    uint8_t goal_kind;      /* GOAL_KIND: the kind of the lines */
    uint16_t target;        /* lines or tiles; points for GOAL_POINTS */
    uint16_t count;
    uint8_t breaks;
    uint8_t kinds;          /* kinds in play: 1..kinds */
    uint8_t joker;          /* the joker comes now and then */
    uint8_t new_kind;       /* a kind (or JOKER) appears for the first time in this shift */

    uint8_t step_ticks;     /* ticks per belt slot */
    uint8_t step_pos;       /* progress in the current step, 0..step_ticks - 1 */
    uint8_t half;           /* the tiles are half way to the next slot */
    uint8_t steps;          /* belt steps (for the rollers) */
    uint8_t spawn_wait;     /* belt steps until the next tile */
    uint8_t last_lane;
    uint8_t bag[2 * KINDS_MAX];
    uint8_t bag_n;

    uint8_t belt[LANES][SLOTS];
    uint8_t cart;           /* lane of the cart */
    uint8_t stack[CART_MAX]; /* stack[stack_n - 1] is the top tile */
    uint8_t stack_n;

    uint8_t bin[CELLS];
    uint8_t marked[CELLS];
    uint8_t fall[CELLS];    /* half rows a tile is still above its cell (display) */
    uint8_t clearing;       /* ticks left of the flash of cleared lines, 0 = none */
    uint8_t chain;          /* lines in the current cascade = multiplier */
    uint8_t need_sweep;     /* the bin changed: look for lines once the tiles settled */

    uint16_t bonus_empty, bonus_shift;
    uint8_t repaired;       /* the bonus replaced a broken tile */

    /* for the renderer: the belt changed, the columns of the bin that changed (bits) */
    uint8_t belt_dirty, bin_dirty;

    Event events[MAX_EVENTS];
    uint8_t nevents;
} Game;

extern Game g;

void game_new(uint16_t seed);
void game_tick(const Input *in);
uint16_t shift_target(uint8_t shift);
uint8_t shift_goal(uint8_t shift);

/* ticks the intro of a shift is shown */
uint16_t intro_ticks(void);

#endif
