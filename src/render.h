/*
 * render.h - the screen of Tilomat: machine, belt, cart, bin, status column, panels and effects.
 */
#ifndef RENDER_H
#define RENDER_H

#include <stdint.h>

extern uint8_t lang;

void render_init(void);
void render_clear(void);
void render_field(void);
void render_frame(void);
void render_events(void);
void render_hide_sprites(void);

/* safe writes to the video RAM while the display is on */
void vset(uint8_t win, uint8_t x, uint8_t y, uint8_t t);
void vfill(uint8_t win, uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint8_t t);

/* a tile of kind k (16 x 8, two map cells) */
void put_kind(uint8_t win, uint8_t x, uint8_t y, uint8_t k);

/* text output: on the background (win = 0) or in the window (win = 1) */
void print(uint8_t win, uint8_t x, uint8_t y, uint8_t text, uint32_t num);
void print_center(uint8_t win, uint8_t y, uint8_t text, uint32_t num);
void print_num(uint8_t win, uint8_t x, uint8_t y, uint32_t num, uint8_t digits);
uint8_t text_width(uint8_t text, uint32_t num);

/* panels: a box of h rows in the window at the bottom of the screen */
void panel_open(uint8_t h);
void panel_show(void);
void panel_close(void);
void panel_line(uint8_t y, uint8_t text, uint32_t num);
void panel_value(uint8_t y, uint8_t text, uint32_t label_num, uint32_t value);

#endif
