/*
 * save.c - the high score list and the settings in the battery-backed cartridge RAM
 * (MBC1, 8 KB at $A000): a magic word, the language and the song chosen, the last name
 * entered and the best 10 results, with a checksum. A missing or damaged list starts empty.
 */
#include <gb/gb.h>
#include <string.h>
#include "save.h"
#include "render.h"
#include "gfx.h"
#include "sound.h"

#define MAGIC 0x54494c31UL     /* "TIL1" */

typedef struct {
    uint32_t magic;
    uint8_t lang;
    uint8_t last_name[NAME_LEN];
    Entry scores[SCORES];
    uint8_t sum;
} Save;

Entry scores[SCORES];
uint8_t last_name[NAME_LEN];

static Save * const sram = (Save *)0xa000;
static Save buf;

static uint8_t checksum(const Save *s)
{
    const uint8_t *p = (const uint8_t *)s;
    uint8_t sum = 0x5a;
    uint16_t i;

    for (i = 0; i < sizeof(Save) - 1; i++)
        sum = (sum << 1 | sum >> 7) + p[i];
    return sum;
}

void save_load(void)
{
    uint8_t i;

    ENABLE_RAM;
    memcpy(&buf, sram, sizeof(Save));
    DISABLE_RAM;
    if (buf.magic != MAGIC || buf.sum != checksum(&buf)) {
        memset(&buf, 0, sizeof(Save));
        for (i = 0; i < NAME_LEN; i++)
            buf.last_name[i] = T_SPACE;
    }
    /* the song chosen is kept above the language bit */
    lang = buf.lang & 1;
    music_song = (buf.lang >> 1) % SONGS;
    memcpy(last_name, buf.last_name, NAME_LEN);
    memcpy(scores, buf.scores, sizeof(scores));
}

void save_store(void)
{
    buf.magic = MAGIC;
    buf.lang = lang | music_song << 1;
    memcpy(buf.last_name, last_name, NAME_LEN);
    memcpy(buf.scores, scores, sizeof(scores));
    buf.sum = checksum(&buf);
    ENABLE_RAM;
    memcpy(sram, &buf, sizeof(Save));
    DISABLE_RAM;
}

/* the place (0..9) a score would take in the list, SCORES if none */
uint8_t score_rank(uint32_t score)
{
    uint8_t i;

    if (!score)
        return SCORES;
    for (i = 0; i < SCORES; i++)
        if (score > scores[i].score)
            return i;
    return SCORES;
}

void score_insert(uint8_t rank, const uint8_t *name, uint32_t score, uint8_t shift)
{
    uint8_t i;

    for (i = SCORES - 1; i > rank; i--)
        scores[i] = scores[i - 1];
    memcpy(scores[rank].name, name, NAME_LEN);
    scores[rank].score = score;
    scores[rank].shift = shift;
    save_store();
}
