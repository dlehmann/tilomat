/*
 * save.h - the high score list and the settings in the battery-backed cartridge RAM.
 */
#ifndef SAVE_H
#define SAVE_H

#include <stdint.h>

#define SCORES 10
#define NAME_LEN 8

typedef struct {
    /* the name as tile numbers */
    uint8_t name[NAME_LEN];
    uint32_t score;
    uint8_t shift;
} Entry;

extern Entry scores[SCORES];
extern uint8_t last_name[NAME_LEN];

void save_load(void);
void save_store(void);
uint8_t score_rank(uint32_t score);
void score_insert(uint8_t rank, const uint8_t *name, uint32_t score, uint8_t shift);

#endif
