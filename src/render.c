/*
 * render.c - the screen of Tilomat.
 *
 * The playfield takes the left 12 columns: the machine in row 0, the belt in rows 1..9 between
 * two rails, the cart in row 10 and the bin below (rim in row 11, the cells in rows 12..16,
 * the floor in row 17). Right of it, behind a dividing line, the status column.
 *
 * Belt and bin are background tiles: a tile of the game is 16 x 8 pixels, two map cells.
 * On the belt it moves in half rows (see tools/gfx.py), in the bin it falls row by row.
 * Only what changes is written: belt_scr[] and bin_scr[] remember the left tile shown in
 * each cell. The rollers of the rails turn by changing their tile data.
 *
 * The cart and the tiles on it, broken tiles and floating points are sprites. Panels (shift
 * intro, summary, pause, game over) are drawn into the window at the bottom of the screen.
 */
#include <gb/gb.h>
#include <gb/hardware.h>
#include <string.h>
#include "render.h"
#include "game.h"
#include "gfx.h"
#include "text.h"
#include "sound.h"

#define CH(c) (T_FONT + (c) - 32)

#define ROW_MACH  0
#define ROW_BELT  1         /* slot SLOTS - 1; slot 0 is in row ROW_BELT + SLOTS - 1 */
#define ROW_CART  10
#define CART_FLOOR 2        /* the first row of the cart's floor in its sprite */
#define ROW_RIM   11
#define ROW_FLOOR 17
#define COL_RAIL_R 11
#define COL_DIV   12
#define HUD_X     13
#define DISP_ROWS (BIN_ROWS + 1)    /* the bin and the rim above it */

uint8_t lang;
volatile uint8_t vbl_count;

static uint8_t belt_scr[LANES * SLOTS];
/* the belt is drawn by changes, see draw_belt() */
#define BELT_CELLS (LANES * SLOTS)
#define MAX_DRAWN 24
static uint8_t *belt_addr[BELT_CELLS];     /* cell = lane * SLOTS + row */
static uint8_t belt_cell_want[BELT_CELLS];
static uint8_t drawn[MAX_DRAWN], drawn_n, now[MAX_DRAWN], now_n;
static uint8_t dl_lane, dl_s, dl_k, dl_t, dl_c, dl_i;
static const uint8_t *dl_b;
static uint8_t bin_scr[LANES][DISP_ROWS];
static uint8_t nozzle[LANES];
static uint8_t rail_frame;

/* the status column as last drawn */
static uint32_t hud_score, hud_left;
static uint8_t hud_breaks, hud_shift, hud_warn;

/* tiles falling off the belt */
#define CRASHES 4
typedef struct {
    uint8_t timer, lane, kind;
} Crash;
static Crash crashes[CRASHES];

/* floating points */
#define POPUPS 2
#define POPUP_TICKS 60
typedef struct {
    uint8_t timer, x, y, n;
    uint8_t t[8];
} Popup;
static Popup popups[POPUPS];
static uint8_t next_popup;

static uint8_t panel_on, nspr, last_nspr;
static uint8_t frame_count;

/* ---------------------------------------------------------------- interrupts */

/* tiles whose data changes in the next vertical blank (the rollers) */
#define ANIM_QUEUE 4
static volatile uint8_t anim_n;
static uint8_t anim_tile[ANIM_QUEUE];
static const uint8_t *anim_src[ANIM_QUEUE];

/*
 * Writes to the background map wait in a queue for the vertical blank: while the display
 * runs, every single write would have to wait for the next horizontal blank. The queue is
 * three arrays of 256 bytes at fixed addresses (tile, address low, high), so the index
 * wraps by itself and q_drain() finds a record with a few instructions: in assembler, up to
 * 48 of them in a blank (about 110 cycles each), as long as the blank lasts.
 */
#define Q_PER_BLANK 48
__at(0xd000) uint8_t q_tile[256];
__at(0xd100) uint8_t q_lo[256];
__at(0xd200) uint8_t q_hi[256];
static volatile uint8_t q_head, q_tail;

static void qset(uint8_t x, uint8_t y, uint8_t t);
static void qflush(void);

static void q_drain(void) __naked
{
    __asm
    ld  a, (_q_head)
    ld  b, a
    ld  a, (_q_tail)
    ld  c, a
    ld  d, #Q_PER_BLANK
00001$:
    ld  a, c
    cp  a, b
    jr  z, 00002$
    ldh a, (_STAT_REG)
    and a, #3
    dec a
    jr  nz, 00002$
    ld  h, #0xd0
    ld  l, c
    ld  e, (hl)
    inc h
    ld  a, (hl)
    inc h
    ld  h, (hl)
    ld  l, a
    ld  (hl), e
    inc c
    dec d
    jr  nz, 00001$
00002$:
    ld  a, c
    ld  (_q_tail), a
    ret
    __endasm;
}

static void vbl_isr(void)
{
    uint8_t i, *d;
    const uint8_t *src;

    vbl_count++;
    if (panel_on)
        LCDC_REG |= LCDCF_WINON;
    q_drain();
    while (anim_n) {
        anim_n--;
        d = (uint8_t *)0x8000 + ((uint16_t)anim_tile[anim_n] << 4);
        src = anim_src[anim_n];
        for (i = 0; i < 16; i++)
            *d++ = *src++;
    }
}

/* changes the data of a tile in a vertical blank */
static void anim(uint8_t tile, const uint8_t *src)
{
    CRITICAL {
        if (anim_n < ANIM_QUEUE) {
            anim_tile[anim_n] = tile;
            anim_src[anim_n] = src;
            anim_n++;
        }
    }
}

void render_init(void)
{
    DISPLAY_OFF;
    /* all tiles at $8000 (the sprite area), where background and sprites share them */
    set_sprite_data(0, GFX_TILES, gfx_tiles);
    BGP_REG = 0xe4;
    OBP0_REG = 0xd0;        /* 1 white, 2 light gray, 3 black */
    OBP1_REG = 0xe0;        /* 1 white, 2 dark gray, 3 black */
    WX_REG = 7;
    WY_REG = 144;
    SCX_REG = 0;
    SCY_REG = 0;
    render_clear();
    CRITICAL {
        add_VBL(vbl_isr);
    }
    set_interrupts(VBL_IFLAG);
    LCDC_REG = LCDCF_ON | LCDCF_WIN9C00 | LCDCF_WINOFF | LCDCF_BG8000 | LCDCF_BG9800 | LCDCF_OBJ8 | LCDCF_OBJON |
               LCDCF_BGON;
}

/* black screen, no panel, no sprites */
void render_clear(void)
{
    panel_close();
    render_hide_sprites();
    qflush();
    DISPLAY_OFF;
    fill_bkg_rect(0, 0, 32, 18, T_SPACE);
    DISPLAY_ON;
}

void render_hide_sprites(void)
{
    uint8_t i;

    for (i = 0; i < 40; i++)
        move_sprite(i, 0, 0);
    last_nspr = 0;
}

/* ---------------------------------------------------------------- video RAM */

/*
 * The background goes through the queue. In the window, the check that the video RAM is
 * accessible and the write must not be split by an interrupt, or the write may fall into the
 * time the display reads the RAM and be lost.
 */
void vset(uint8_t win, uint8_t x, uint8_t y, uint8_t t)
{
    uint8_t *a = (uint8_t *)0x9c00 + ((uint16_t)y << 5) + x;

    if (!win) {
        qset(x, y, t);
        return;
    }

    CRITICAL {
        while (STAT_REG & STATF_BUSY)
            ;
        *a = t;
    }
}

void vfill(uint8_t win, uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint8_t t)
{
    uint8_t i, j;

    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++)
            vset(win, x + i, y + j, t);
}

/* a background map cell, written in a vertical blank (waits while the queue is full) */
static void qset_addr(uint8_t *a, uint8_t t)
{
    uint8_t h = q_head;

    while ((uint8_t)(h + 1) == q_tail)
        ;
    q_tile[h] = t;
    q_lo[h] = (uint16_t)a & 0xff;
    q_hi[h] = (uint16_t)a >> 8;
    q_head = h + 1;
}

static void qset(uint8_t x, uint8_t y, uint8_t t)
{
    qset_addr((uint8_t *)0x9800 + ((uint16_t)y << 5) + x, t);
}

/* waits until the queued writes are done */
static void qflush(void)
{
    while (q_head != q_tail)
        ;
}

/* a pair of map cells: tile t and t + 1 (two spaces for T_SPACE) */
static void put2(uint8_t x, uint8_t y, uint8_t t)
{
    qset(x, y, t);
    qset(x + 1, y, t == T_SPACE ? T_SPACE : t + 1);
}

static uint8_t kind_tile(uint8_t k)
{
    return T_KIND + (k - 1) * KIND_TILES;
}

void put_kind(uint8_t win, uint8_t x, uint8_t y, uint8_t k)
{
    vset(win, x, y, kind_tile(k));
    vset(win, x + 1, y, kind_tile(k) + 1);
}

/* ---------------------------------------------------------------- text */

static const uint32_t tens[10] = {
    1000000000UL, 100000000UL, 10000000UL, 1000000UL, 100000UL, 10000UL, 1000UL, 100UL, 10UL, 1UL
};

/*
 * The decimal digits of n into buf, most significant first, without leading zeros (at least
 * one digit); returns their number. By subtraction: divisions of 32 bit numbers take
 * thousands of cycles on the Game Boy.
 */
static uint8_t digits_of(uint32_t n, uint8_t *buf)
{
    uint8_t k, d, len = 0;
    uint32_t t;

    for (k = 0; k < 10; k++) {
        t = tens[k];
        for (d = 0; n >= t; d++)
            n -= t;
        if (d || len || k == 9)
            buf[len++] = d;
    }
    return len;
}

static uint8_t num_digits(uint32_t n)
{
    uint8_t buf[10];
    return digits_of(n, buf);
}

/* a number with at least `digits` digits (leading zeros) */
void print_num(uint8_t win, uint8_t x, uint8_t y, uint32_t num, uint8_t digits)
{
    uint8_t buf[10], n = digits_of(num, buf), k;

    for (k = n; k < digits; k++)
        vset(win, x++, y, CH('0'));
    for (k = 0; k < n; k++)
        vset(win, x++, y, CH('0') + buf[k]);
}

static const uint16_t tens16[4] = {10000, 1000, 100, 10};

/* the five decimal digits of a 16 bit number, with leading zeros (fast) */
static void digits16(uint16_t n, uint8_t *buf)
{
    uint8_t k, d;
    uint16_t t;

    for (k = 0; k < 4; k++) {
        t = tens16[k];
        for (d = 0; n >= t; d++)
            n -= t;
        buf[k] = d;
    }
    buf[4] = (uint8_t)n;
}

/* a 16 bit number ending in column x_end, spaces in front up to `width` characters */
static void print_right(uint8_t x_end, uint8_t y, uint16_t num, uint8_t width)
{
    uint8_t buf[5], k = 0, x = x_end + 1 - width;

    digits16(num, buf);
    while (k < 4 && !buf[k])
        k++;
    for (; x + 5 - k <= x_end; x++)
        qset(x, y, T_SPACE);
    for (; k < 5; k++)
        qset(x++, y, CH('0') + buf[k]);
}

/* the score in the status column: 7 digits, updated by adding the difference */
#define SCORE_DIGITS 7
static uint8_t score_shown[SCORE_DIGITS];

static void hud_score_digits(uint32_t old, uint32_t score)
{
    uint8_t buf[10], add[5], k, n, carry = 0, d;

    if (score > 9999999UL)
        score = 9999999UL;
    if (old == 0xffffffffUL || score < old || score - old > 65535UL) {
        n = digits_of(score, buf);
        for (k = 0; k < SCORE_DIGITS; k++) {
            d = k + n >= SCORE_DIGITS ? buf[k + n - SCORE_DIGITS] : 0;
            score_shown[k] = d;
            qset(HUD_X + k, 1, CH('0') + d);
        }
        return;
    }
    digits16((uint16_t)(score - old), add);
    for (k = SCORE_DIGITS; k--;) {
        d = score_shown[k] + carry + (k >= 2 ? add[k - 2] : 0);
        carry = d >= 10;
        if (carry)
            d -= 10;
        if (d != score_shown[k]) {
            score_shown[k] = d;
            qset(HUD_X + k, 1, CH('0') + d);
        }
    }
}

static uint8_t line_width(const uint8_t *s, uint32_t num)
{
    uint8_t w = 0;
    for (; *s != TXT_END && *s != TXT_NL; s++)
        w += *s == TXT_NUM ? num_digits(num) : 1;
    return w;
}

uint8_t text_width(uint8_t text, uint32_t num)
{
    return line_width(texts[text][lang], num);
}

static void print_line(uint8_t win, uint8_t x, uint8_t y, const uint8_t *s, uint32_t num)
{
    for (; *s != TXT_END && *s != TXT_NL; s++) {
        if (*s == TXT_NUM) {
            print_num(win, x, y, num, 1);
            x += num_digits(num);
        } else {
            vset(win, x++, y, *s);
        }
    }
}

static void print_text(uint8_t win, uint8_t x, uint8_t y, uint8_t text, uint32_t num, uint8_t center)
{
    const uint8_t *s = texts[text][lang];
    for (;;) {
        print_line(win, center ? (20 - line_width(s, num)) / 2 : x, y, s, num);
        while (*s != TXT_END && *s != TXT_NL)
            s++;
        if (*s == TXT_END)
            return;
        s++;
        y++;
    }
}

void print(uint8_t win, uint8_t x, uint8_t y, uint8_t text, uint32_t num)
{
    print_text(win, x, y, text, num, 0);
}

void print_center(uint8_t win, uint8_t y, uint8_t text, uint32_t num)
{
    print_text(win, 0, y, text, num, 1);
}

/* ---------------------------------------------------------------- panels */

void panel_open(uint8_t h)
{
    uint8_t y;

    panel_close();
    render_hide_sprites();
    vfill(1, 0, 0, 20, h, T_SPACE);
    vset(1, 0, 0, T_BOX_TL);
    vfill(1, 1, 0, 18, 1, T_BOX_T);
    vset(1, 19, 0, T_BOX_TR);
    for (y = 1; y < h - 1; y++) {
        vset(1, 0, y, T_BOX_L);
        vset(1, 19, y, T_BOX_R);
    }
    vset(1, 0, h - 1, T_BOX_BL);
    vfill(1, 1, h - 1, 18, 1, T_BOX_B);
    vset(1, 19, h - 1, T_BOX_BR);
    WY_REG = (18 - h) * 8;
}

/* shows the panel once its text is written */
void panel_show(void)
{
    panel_on = 1;
}

void panel_close(void)
{
    panel_on = 0;
    LCDC_REG &= ~LCDCF_WINON;
}

void panel_line(uint8_t y, uint8_t text, uint32_t num)
{
    print_center(1, y, text, num);
}

/* a label on the left and a value with "+" on the right */
void panel_value(uint8_t y, uint8_t text, uint32_t label_num, uint32_t value)
{
    uint8_t d = num_digits(value);
    print(1, 1, y, text, label_num);
    vset(1, 18 - d, y, CH('+'));
    print_num(1, 19 - d, y, value, 1);
}

/* ---------------------------------------------------------------- the field */

static void hud_goal(void)
{
    static const uint8_t goal_text[5] = {HUD_G_LINES, HUD_G_TILES, HUD_G_KIND, HUD_G_POINTS, HUD_G_BIG};

    vfill(0, HUD_X, 9, 7, 2, T_SPACE);
    print(0, HUD_X, 9, goal_text[g.goal], 0);
    if (g.goal == GOAL_KIND)
        put_kind(0, HUD_X + 4, 10, g.goal_kind);
}

/* draws the whole field of a new shift */
void render_field(void)
{
    uint8_t x, y, i;

    render_hide_sprites();
    qflush();
    DISPLAY_OFF;
    fill_bkg_rect(0, 0, 32, 18, T_SPACE);
    /* the machine */
    set_bkg_tile_xy(0, ROW_MACH, T_MACH);
    set_bkg_tile_xy(COL_RAIL_R, ROW_MACH, T_MACH);
    for (i = 0; i < LANES; i++) {
        set_bkg_tile_xy(1 + 2 * i, ROW_MACH, T_NOZ_L);
        set_bkg_tile_xy(2 + 2 * i, ROW_MACH, T_NOZ_R);
        nozzle[i] = 0;
    }
    /* the belt */
    for (y = 0; y < SLOTS; y++) {
        set_bkg_tile_xy(0, ROW_BELT + y, T_RAIL);
        set_bkg_tile_xy(COL_RAIL_R, ROW_BELT + y, T_RAIL);
        for (i = 0; i < LANES; i++) {
            set_bkg_tile_xy(1 + 2 * i, ROW_BELT + y, T_BELT_L);
            set_bkg_tile_xy(2 + 2 * i, ROW_BELT + y, T_BELT_R);
            belt_scr[i * SLOTS + y] = belt_cell_want[i * SLOTS + y] = T_BELT_L;
            belt_addr[i * SLOTS + y] = (uint8_t *)0x9800 + ((uint16_t)(ROW_BELT + y) << 5) + 1 + 2 * i;
        }
    }
    /* the bin */
    set_bkg_tile_xy(0, ROW_RIM, T_RIM_L);
    set_bkg_tile_xy(COL_RAIL_R, ROW_RIM, T_RIM_R);
    for (i = 0; i < LANES; i++)
        bin_scr[i][BIN_ROWS] = T_SPACE;
    for (y = 0; y < BIN_ROWS; y++) {
        set_bkg_tile_xy(0, ROW_RIM + 1 + y, T_BIN_L);
        set_bkg_tile_xy(COL_RAIL_R, ROW_RIM + 1 + y, T_BIN_R);
        for (i = 0; i < LANES; i++) {
            set_bkg_tile_xy(1 + 2 * i, ROW_RIM + 1 + y, T_CELL_L);
            set_bkg_tile_xy(2 + 2 * i, ROW_RIM + 1 + y, T_CELL_R);
            bin_scr[i][BIN_ROWS - 1 - y] = T_CELL_L;
        }
    }
    set_bkg_tile_xy(0, ROW_FLOOR, T_FLOOR_L);
    for (x = 1; x < COL_RAIL_R; x++)
        set_bkg_tile_xy(x, ROW_FLOOR, T_FLOOR);
    set_bkg_tile_xy(COL_RAIL_R, ROW_FLOOR, T_FLOOR_R);
    /* the status column */
    for (y = 0; y < 18; y++)
        set_bkg_tile_xy(COL_DIV, y, T_DIV);
    DISPLAY_ON;
    print(0, HUD_X, 0, HUD_SCORE, 0);
    print(0, HUD_X, 3, HUD_SHIFT, 0);
    print(0, HUD_X, 7, HUD_ORDER, 0);
    print(0, HUD_X, 12, HUD_LEFT, 0);
    print(0, HUD_X, 15, HUD_BREAKS, 0);
    hud_goal();
    hud_score = hud_left = 0xffffffffUL;
    hud_breaks = hud_shift = hud_warn = 0xff;
    drawn_n = 0;
    rail_frame = 0;
    anim(T_RAIL, rail_frames);
    for (i = 0; i < CRASHES; i++)
        crashes[i].timer = 0;
    for (i = 0; i < POPUPS; i++)
        popups[i].timer = 0;
    g.belt_dirty = 1;
    g.bin_dirty = ALL_COLUMNS;
}

/* the left tile of each kind (kind_tile() without the multiplication) */
#define KT(k) (T_KIND + ((k) - 1) * KIND_TILES)
static const uint8_t kind_base[8] = {0, KT(1), KT(2), KT(3), KT(4), KT(5), KT(6), KT(7)};

/*
 * The belt is drawn by changes: the cells of the tiles drawn last time and of the tiles now
 * are compared with what is shown. (A full comparison of all 45 cells took 80 display lines:
 * SDCC's code is slow on the Game Boy, hence also the static variables.)
 */

static void belt_want(uint8_t c, uint8_t t)
{
    belt_cell_want[c] = t;
    if (now_n < MAX_DRAWN)
        now[now_n++] = c;
}

static void belt_show(uint8_t c)
{
    uint8_t t = belt_cell_want[c];

    if (belt_scr[c] != t) {
        belt_scr[c] = t;
        qset_addr(belt_addr[c], t);
        qset_addr(belt_addr[c] + 1, t + 1);
    }
}

static void draw_belt(void)
{
    for (dl_i = 0; dl_i < drawn_n; dl_i++)
        belt_cell_want[drawn[dl_i]] = T_BELT_L;
    now_n = 0;
    dl_b = &g.belt[0][0];
    for (dl_lane = 0, dl_c = SLOTS - 1; dl_lane < LANES; dl_lane++, dl_c += 2 * SLOTS)
        /* slot s is in row SLOTS - 1 - s: cell dl_c walks up the rows */
        for (dl_s = SLOTS; dl_s; dl_s--, dl_c--) {
            dl_k = *dl_b++ & 0x0f;
            if (!dl_k)
                continue;
            dl_t = kind_base[dl_k];
            if (!g.half) {
                belt_want(dl_c, dl_t);
            } else {
                belt_want(dl_c, dl_t + 2);
                if (dl_s != SLOTS)      /* not in the last row */
                    belt_want(dl_c + 1, dl_t + 4);
            }
        }
    for (dl_i = 0; dl_i < drawn_n; dl_i++)
        belt_show(drawn[dl_i]);
    for (dl_i = 0; dl_i < now_n; dl_i++) {
        belt_show(now[dl_i]);
        drawn[dl_i] = now[dl_i];
    }
    drawn_n = now_n;
}

/* the columns of the bin that changed */
static uint8_t db_col, db_row, db_d, db_t, db_x, db_flash, db_mask;
static uint8_t db_want[DISP_ROWS];
static const uint8_t *db_b, *db_f, *db_m;
static uint8_t *db_scr;

static void draw_bin(uint8_t mask)
{
    db_flash = g.clearing && (g.clearing & 4);
    db_b = g.bin;
    db_f = g.fall;
    db_m = g.marked;
    db_scr = &bin_scr[0][0];
    for (db_col = 0, db_x = 1, db_mask = 1; db_col < LANES; db_col++, db_x += 2, db_mask <<= 1) {
        if (!(mask & db_mask)) {
            db_b += BIN_ROWS;
            db_f += BIN_ROWS;
            db_m += BIN_ROWS;
            db_scr += DISP_ROWS;
            continue;
        }
        for (db_d = 0; db_d < BIN_ROWS; db_d++)
            db_want[db_d] = T_CELL_L;
        db_want[BIN_ROWS] = T_SPACE;
        for (db_row = 0; db_row < BIN_ROWS; db_row++, db_b++, db_f++, db_m++) {
            if (!(db_t = *db_b))
                continue;
            db_d = db_row + ((*db_f + 1) >> 1);
            if (db_d > BIN_ROWS)
                db_d = BIN_ROWS;
            db_want[db_d] = db_flash && *db_m ? T_FLASH_L : kind_base[db_t];
        }
        for (db_d = 0; db_d < DISP_ROWS; db_d++, db_scr++)
            if ((db_t = db_want[db_d]) != *db_scr) {
                *db_scr = db_t;
                put2(db_x, ROW_FLOOR - 1 - db_d, db_t);
            }
    }
}

static uint32_t goal_left(void)
{
    if (g.goal == GOAL_POINTS)
        return g.shift_points >= g.target ? 0 : g.target - g.shift_points;
    return g.count >= g.target ? 0 : g.target - g.count;
}

/* the status column: only what changed */
static void hud(void)
{
    uint8_t i, warn;
    uint32_t left;

    if (hud_score != g.score) {
        hud_score_digits(hud_score, g.score);
        hud_score = g.score;
    }
    if (hud_shift != g.shift) {
        print_right(19, 4, g.shift, 3);
        hud_goal();
        hud_shift = g.shift;
    }
    left = goal_left();
    if (hud_left != left) {
        print_right(19, 13, (uint16_t)left, 5);
        hud_left = left;
    }
    if (hud_breaks != g.breaks) {
        for (i = 0; i < BREAKS - 1; i++)
            qset(HUD_X + 1 + 2 * i, 16, i < g.breaks ? T_BREAK_ON : T_BREAK_OFF);
        hud_breaks = g.breaks;
    }
    warn = g.breaks == BREAKS - 1 && g.phase == PHASE_PLAY && (frame_count & 32);
    if (hud_warn != warn) {
        for (i = 0; i < 7; i++)
            qset(HUD_X + i, 17, T_SPACE);
        if (warn)
            print(0, HUD_X, 17, HUD_WARN, 0);
        hud_warn = warn;
    }
}

/* ---------------------------------------------------------------- sprites */

/* a sprite at screen position x, y (written into the shadow OAM directly: faster) */
static void spr(uint8_t x, uint8_t y, uint8_t t, uint8_t prop)
{
    volatile OAM_item_t *o;

    if (nspr >= 40)
        return;
    o = &shadow_OAM[nspr++];
    o->y = y + 16;
    o->x = x + 8;
    o->tile = t;
    o->prop = prop;
}

static uint8_t kind_prop(uint8_t k)
{
    return (KIND_DARK >> k) & 1 ? S_PALETTE : 0;
}

static void popup(uint8_t col, uint8_t row, uint16_t points, uint8_t mult)
{
    Popup *p = &popups[next_popup];
    uint8_t buf[10], n, k;

    next_popup = next_popup == POPUPS - 1 ? 0 : next_popup + 1;
    n = digits_of(points, buf);
    p->n = 0;
    p->t[p->n++] = (uint8_t)(S_DIGIT + 10);
    for (k = 0; k < n && p->n < 6; k++)
        p->t[p->n++] = S_DIGIT + buf[k];
    if (mult > 1 && mult < 10) {
        p->t[p->n++] = (uint8_t)(S_DIGIT + 11);
        p->t[p->n++] = S_DIGIT + mult;
    }
    k = 8 + col * 16 + 8;
    k = k > p->n * 4 ? k - p->n * 4 : 0;
    if (k + p->n * 8 > 96)
        k = 96 - p->n * 8;
    p->x = k;
    p->y = (ROW_FLOOR - 1 - row) * 8 - 4;
    p->timer = POPUP_TICKS;
}

void render_events(void)
{
    uint8_t i, k;
    Event *e;

    for (i = 0; i < g.nevents; i++) {
        e = &g.events[i];
        switch (e->type) {
        case EV_SPAWN:
            nozzle[e->lane] = 10;
            put2(1 + 2 * e->lane, ROW_MACH, T_NOZ_LIT_L);
            break;
        case EV_BREAK:
            for (k = 0; k < CRASHES; k++)
                if (!crashes[k].timer) {
                    crashes[k].timer = 1;
                    crashes[k].lane = e->lane;
                    crashes[k].kind = e->kind;
                    break;
                }
            break;
        case EV_LINES:
            popup(e->lane, e->row, e->points, e->mult);
            break;
        }
    }
}

#define CRASH_FALL 10
#define CRASH_TICKS 30

static void sprites(void)
{
    uint8_t i, k, x, y, t, prop;
    Crash *c;
    Popup *p;

    nspr = 0;
    if (g.phase == PHASE_PLAY || g.phase == PHASE_DONE) {
        /* the cart and its tiles */
        x = 8 + g.cart * 16;
        /* overlapping like cards: the top tile whole, of each one below the lower half; the
           bottom one stands on the floor of the cart, between its posts. The top tile first,
           then the cart: the sprite drawn first wins where they overlap */
        for (i = g.stack_n; i--; ) {
            k = KIND(g.stack[i]);
            prop = kind_prop(k);
            y = ROW_CART * 8 + CART_FLOOR - 8 - 4 * i;
            spr(x, y, S_KIND + 2 * (k - 1), prop);
            spr(x + 8, y, S_KIND + 2 * (k - 1) + 1, prop);
        }
        t = g.stack_n == CART_MAX ? S_CART_FULL : S_CART;
        spr(x, ROW_CART * 8, t, 0);
        spr(x + 8, ROW_CART * 8, t + 1, 0);
    }
    /* broken tiles: fall past the cart, then the shards fly */
    for (i = 0, c = crashes; i < CRASHES; i++, c++) {
        if (!c->timer)
            continue;
        x = 8 + c->lane * 16;
        if (c->timer < CRASH_FALL) {
            y = ROW_CART * 8 - 4 + c->timer * 2;
            prop = kind_prop(c->kind);
            spr(x, y, S_KIND + 2 * (c->kind - 1), prop);
            spr(x + 8, y, S_KIND + 2 * (c->kind - 1) + 1, prop);
        } else {
            k = c->timer - CRASH_FALL;
            y = ROW_RIM * 8 - 2 + (k >> 1) + ((k * k) >> 5);
            if (k < CRASH_TICKS - CRASH_FALL - 4 || (k & 1)) {
                spr(x + 2 - (k >> 1), y - 2, S_SHARD, 0);
                spr(x + 6 + (k >> 1), y, S_SHARD2, 0);
            }
        }
        if (++c->timer >= CRASH_TICKS)
            c->timer = 0;
    }
    /* floating points */
    for (i = 0, p = popups; i < POPUPS; i++, p++) {
        if (!p->timer)
            continue;
        p->timer--;
        y = p->y - ((POPUP_TICKS - p->timer) >> 2);
        if (p->timer > 12 || (p->timer & 2))
            for (k = 0; k < p->n; k++)
                spr(p->x + k * 6, y, p->t[k], 0);
    }
    for (i = nspr; i < last_nspr; i++)
        shadow_OAM[i].y = 0;
    last_nspr = nspr;
}

void render_frame(void)
{
    uint8_t i, f;

    frame_count++;
    for (i = 0; i < LANES; i++)
        if (nozzle[i] && !--nozzle[i])
            put2(1 + 2 * i, ROW_MACH, T_NOZ_L);
    f = (g.steps + g.half) & 1;
    if (f != rail_frame) {
        rail_frame = f;
        anim(T_RAIL, rail_frames + (f << 4));
    }
    if (g.belt_dirty) {
        g.belt_dirty = 0;
        draw_belt();
    }
    if (g.bin_dirty) {
        draw_bin(g.bin_dirty);
        g.bin_dirty = 0;
    }
    hud();
    if (!panel_on)
        sprites();
}
