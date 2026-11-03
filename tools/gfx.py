#!/usr/bin/env python3
"""
gfx.py - the graphics of Tilomat: all 8x8 tiles in the four shades of the Game Boy.

The background and the sprites share one table of up to 256 tiles (LCDC bit 4 set, tiles at
$8000). The tiles are drawn here from small pictures in text form ('.' white, '-' light gray,
'+' dark gray, '#' black, ' ' transparent or what lies below) and from the font Press Start 2P.

A Tilomat tile is 16 x 8 pixels (two 8x8 tiles side by side): flat on the belt, stacked in the
bin. Its kinds have to be told apart in four shades, so they differ by pattern, not by color.
On the belt a tile moves in half rows: the "A" tiles show its upper half in the lower half of
a cell, the "B" tiles its lower half in the upper half of the next one.

Output: gen/gfx.c, gen/gfx.h (tile numbers), build/tiles.png (a preview of all tiles)

Usage: tools/gfx.py <gen dir> [preview.png] [dir for the tile kinds as PNGs, for the web page]
"""
import os
import sys

from PIL import Image, ImageDraw, ImageFont

OUT = sys.argv[1] if len(sys.argv) > 1 else 'gen'
PREVIEW = sys.argv[2] if len(sys.argv) > 2 else None
KINDS_DIR = sys.argv[3] if len(sys.argv) > 3 else None
FONT = os.path.join(os.path.dirname(__file__), 'fonts', 'PressStart2P-Regular.ttf')

SHADE = {'.': 0, '-': 1, '+': 2, '#': 3}
tiles = []          # list of 64 shades (0..3) each
names = []          # (name, index) for gfx.h
extra = []          # extra C arrays (animation frames): (name, [tiles])
defines = []        # further constants for gfx.h


def pic(rows, under=None):
    """A tile from 8 strings of 8 characters; ' ' shows `under` (a tile) or is 0."""
    out = []
    for y in range(8):
        row = (rows[y] if y < len(rows) else '').ljust(8)
        for x in range(8):
            ch = row[x]
            if ch == ' ':
                out.append(under[y * 8 + x] if under else 0)
            else:
                out.append(SHADE[ch])
    return out


def add(name, t):
    """Appends a tile and returns its number."""
    tiles.append(t)
    if name:
        names.append((name, len(tiles) - 1))
    return len(tiles) - 1


def halves(rows16):
    """The left and right 8x8 tile of a picture 16 pixels wide."""
    return pic([r[:8] for r in rows16]), pic([r[8:] for r in rows16])


def encode(t):
    """2bpp Game Boy tile data: per row the low bits, then the high bits."""
    data = []
    for y in range(8):
        lo = hi = 0
        for x in range(8):
            v = t[y * 8 + x]
            lo |= (v & 1) << (7 - x)
            hi |= (v >> 1) << (7 - x)
        data += [lo, hi]
    return data


# ---------------------------------------------------------------- font
# tiles 0..63: ASCII 32..95, white letters on black; then the umlauts and symbols

font = ImageFont.truetype(FONT, 8)


def glyph(ch):
    """The pixels of a character of Press Start 2P (1 where set)."""
    im = Image.new('1', (8, 16), 0)
    ImageDraw.Draw(im).text((0, 0), ch, font=font, fill=1)
    return [[im.getpixel((x, y)) for x in range(8)] for y in range(8)]


def char_tile(g):
    return [0 if g[y][x] else 3 for y in range(8) for x in range(8)]


for c in range(32, 96):
    add('T_SPACE' if c == 32 else None, char_tile(glyph(chr(c))))
names.append(('T_FONT', 0))

# the umlauts: the dots in row 0 above a letter squeezed into rows 1..6
for ch, base in (('AE', 'A'), ('OE', 'O'), ('UE', 'U')):
    g = glyph(base)
    t = [[0] * 8 for _ in range(8)]
    t[0][1] = t[0][2] = t[0][4] = t[0][5] = 1
    for y, src in enumerate([g[0], g[2], g[3], g[4], g[5], g[6]]):
        t[y + 1] = src
    add('T_' + ch, char_tile(t))
add('T_SZ', char_tile(glyph('ß')))
add('T_CURSOR', pic(['########'] * 6 + ['#......#', '########']))

# the frame of the panels (light lines on black)
add('T_BOX_TL', pic(['########'] * 3 + ['###-----'] + ['###-####'] * 4))
add('T_BOX_T', pic(['########'] * 3 + ['--------'] + ['########'] * 4))
add('T_BOX_TR', pic(['########'] * 3 + ['-----###'] + ['####-###'] * 4))
add('T_BOX_L', pic(['###-####'] * 8))
add('T_BOX_R', pic(['####-###'] * 8))
add('T_BOX_BL', pic(['###-####'] * 3 + ['###-----'] + ['########'] * 4))
add('T_BOX_B', pic(['########'] * 3 + ['--------'] + ['########'] * 4))
add('T_BOX_BR', pic(['####-###'] * 3 + ['-----###'] + ['########'] * 4))

# ---------------------------------------------------------------- the tiles


def kind_pixels(k):
    """The 16 x 8 picture of tile kind k (1..6, 7 = joker): a black frame around 14 x 6."""
    rows = []
    star = ['   #   ', '  ###  ', '#######', ' ##### ', ' ## ## ', '#     #']
    for y in range(8):
        r = ''
        for x in range(16):
            if y in (0, 7) or x in (0, 15):
                r += '#'
                continue
            ix, iy = x - 1, y - 1         # 0..13, 0..5
            if k == 1:      # white, shaded at the bottom right
                ch = '-' if iy == 5 or ix == 13 else '.'
            elif k == 2:    # black with a white inner frame
                ch = '.' if ix in (0, 13) or iy in (0, 5) else '#'
            elif k == 3:    # dark gray with a light gray inner frame
                ch = '-' if ix in (0, 13) or iy in (0, 5) else '+'
            elif k == 4:    # checkered
                ch = '#' if (ix // 2 + iy // 2) % 2 else '.'
            elif k == 5:    # diagonal stripes
                ch = '+' if ((ix + iy) // 2) % 2 else '.'
            elif k == 6:    # white with a black diamond
                ch = '#' if abs(ix - 6.5) + 2 * abs(iy - 2.5) <= 4.5 else '.'
            else:           # joker: a star on light gray
                sx = ix - 3
                ch = star[iy][sx] if 0 <= sx < 7 and star[iy][sx] == '#' else '-'
            r += ch
        rows.append(r)
    return rows


KINDS = 7
BELT = ['++++++++++++++++'] * 3 + ['#++++++++++++++#'] + ['++++++++++++++++'] * 3 + ['#++++++++++++++#']
BELT = [('#' + r[1:15] + '#') for r in BELT]

# per kind 6 tiles: full L/R, A L/R (upper half of the tile in the lower half of the cell), B L/R
for k in range(1, KINDS + 1):
    p = kind_pixels(k)
    a = BELT[:4] + p[:4]
    b = p[4:] + BELT[4:]
    for i, rows in enumerate((p, a, b)):
        lt, rt = halves(rows)
        add('T_KIND' if k == 1 and i == 0 else None, lt)
        add(None, rt)
defines.append(('KIND_TILES', 6))

lt, rt = halves(BELT)
add('T_BELT_L', lt)
add('T_BELT_R', rt)

# a cell of the bin: black with the corners of the grid
CELL = ['+              +'.replace(' ', '#')] + ['################'] * 6 + ['+##############+']
lt, rt = halves(CELL)
add('T_CELL_L', lt)
add('T_CELL_R', rt)
# a tile of a line flashing before it goes
FLASH = ['################'] + ['#..............#'] * 6 + ['################']
lt, rt = halves(FLASH)
add('T_FLASH_L', lt)
add('T_FLASH_R', rt)

# the side rails of the belt, with rollers moving down (two frames, 4 pixels apart)
RAIL = ['#-.-+#', '#-.-+#', '#+-+##', '##+###', '#-.-+#', '#-.-+#', '#+-+##', '##+###']


def rail(shift):
    rows = RAIL[-shift:] + RAIL[:-shift] if shift else RAIL
    return pic([('#' + r + '#') for r in rows])


add('T_RAIL', rail(0))
extra.append(('rail_frames', [rail(0), rail(4)]))

# the belt of the title screen, seen from the side (16 x 16, two rows): the band on top with
# grooves moving right, rollers turning clockwise, the band below moving left; SIDE_FRAMES
# frames, one pixel apart (the grooves repeat every 4 pixels, a roller turns once in 24)
ROLLER = ['  ----  ', ' -++++- ', '-++++++-', '-++..++-', '-++..++-', '-++++++-', ' -++++- ', '  ----  ']
MARK = [((3, 1), (4, 1)), ((6, 3), (6, 4)), ((3, 6), (4, 6)), ((1, 3), (1, 4))]


SIDE_FRAMES = 24


def side_belt(f):
    rows = ['-' * 16, ''.join('#' if (x - f) % 4 == 0 else '+' for x in range(16)), '+' * 16, '#' * 16]
    roller = [list(r) for r in ROLLER]
    for x, y in MARK[f * 4 // SIDE_FRAMES]:
        roller[y][x] = '#'
    rows += ['####' + ''.join(r).replace(' ', '#') + '####' for r in roller]
    rows += ['+' * 16, ''.join('#' if (x + f) % 4 == 0 else '+' for x in range(16)), '-' * 16, '#' * 16]
    upper, lower = rows[:8], rows[8:]
    return [*halves(upper), *halves(lower)]


for n, t in zip(('T_SIDE_UL', 'T_SIDE_UR', 'T_SIDE_LL', 'T_SIDE_LR'), side_belt(0)):
    add(n, t)
extra.append(('side_frames', [t for f in range(SIDE_FRAMES) for t in side_belt(f)]))
defines.append(('SIDE_FRAMES', SIDE_FRAMES))

# the machine above the belt: housing and a nozzle with a lamp for each lane
add('T_MACH', pic(['########', '#-------', '#-+++++-', '#-+###+-', '#-+++++-', '#-------', '########',
                   '+#+#+#+#']))
NOZ = ['################', '+--------------+', '+-+#+#+#+#+#+#-+', '+--------------+', '##+----------+##',
       '###+--    --+###', '####+------+####', '#####+####+#####']
lt, rt = halves(NOZ)
add('T_NOZ_L', lt)
add('T_NOZ_R', rt)
LIT = [r.replace('    ', '....') for r in NOZ]
LIT[4] = '##+-........-+##'
lt, rt = halves(LIT)
add('T_NOZ_LIT_L', lt)
add('T_NOZ_LIT_R', rt)

# the bin: walls, rim and floor
add('T_BIN_L', pic(['#-.+####'] * 8))
add('T_BIN_R', pic(['####+.-#'] * 8))
add('T_RIM_L', pic(['########', '########', '########', '########', '#-.-----', '#-.+++++', '#-.+####',
                    '#-.+####']))
add('T_RIM_R', pic(['########', '########', '########', '########', '-----.-#', '+++++.-#', '####+.-#',
                    '####+.-#']))
add('T_FLOOR_L', pic(['#-.+####', '#-.+++++', '#-......', '#-------', '#+++++++', '########', '########',
                      '########']))
add('T_FLOOR', pic(['########', '++++++++', '........', '--------', '++++++++', '########', '########',
                    '########']))
add('T_FLOOR_R', pic(['####+.-#', '+++++.-#', '......-#', '-------#', '+++++++#', '########', '########',
                      '########']))
# the dividing line left of the status panel
add('T_DIV', pic(['##-.-###'] * 8))
# the break meter: an empty slot and a broken tile
add('T_BREAK_OFF', pic(['########', '#++++++#', '#+####+#', '#+####+#', '#+####+#', '#+####+#', '#++++++#',
                        '########']))
add('T_BREAK_ON', pic(['########', '#...#..#', '#..#...#', '#.##..##', '##..#..#', '#..#...#', '#...#..#',
                       '########']))

# ---------------------------------------------------------------- the logo: TILOMAT in big letters


def big(ch):
    """A letter of 16 x 16 pixels: the font doubled, white above, light gray below, a dark shadow."""
    g = glyph(ch)
    px = [[3] * 16 for _ in range(16)]
    for y in range(8):
        for x in range(8):
            if g[y][x]:
                for yy in range(2):
                    for xx in range(2):
                        sx, sy = x * 2 + xx + 1, y * 2 + yy + 1
                        if sx < 16 and sy < 16 and px[sy][sx] == 3:
                            px[sy][sx] = 2
    for y in range(8):
        for x in range(8):
            if g[y][x]:
                for yy in range(2):
                    for xx in range(2):
                        sy = y * 2 + yy
                        px[sy][x * 2 + xx] = 0 if sy < 7 else 1
    return [[px[ty * 8 + y][tx * 8 + x] for y in range(8) for x in range(8)] for ty in range(2) for tx in range(2)]


for ch in 'TILOMA':
    q = big(ch)
    for k in range(4):
        add('T_BIG_' + ch if k == 0 else None, q[k])

# ---------------------------------------------------------------- sprites (color 0 is transparent)
# palette OBP0: 1 white, 2 light gray, 3 black; OBP1: 1 light gray, 2 dark gray, 3 black

SPR = {' ': ' ', 'w': '-', 'l': '+', 'k': '#'}


def spr(rows):
    return pic([''.join(SPR[c] for c in r) for r in rows])


def to_sprite(rows16, dark=False):
    """A picture with shades to sprite colors: for OBP0 white 1, light gray 2, black 3; for OBP1
    (dark) white or light gray 1, dark gray 2, black 3."""
    m = {'.': 'w', '-': 'w' if dark else 'l', '+': 'l', '#': 'k', ' ': ' '}
    return [''.join(m[c] for c in r) for r in rows16]


# the tiles as sprites (on the cart and falling off the belt)
dark = 0
for k in range(1, KINDS + 1):
    p = kind_pixels(k)
    d = any('+' in r for r in p)
    if d:
        dark |= 1 << k
    s = to_sprite(p, d)
    add('S_KIND' if k == 1 else None, spr([r[:8] for r in s]))
    add(None, spr([r[8:] for r in s]))
defines.append(('KIND_DARK', dark))

CART = ['k              k',
        'kw            wk',
        'kkkkkkkkkkkkkkkk',
        'kwwwwwwwwwwwwwwk',
        'kwlwlwlwlwlwlwlk',
        'kkkkkkkkkkkkkkkk',
        ' kk          kk ',
        ' kk          kk ']
add('S_CART', spr([r[:8] for r in CART]))
add(None, spr([r[8:] for r in CART]))
FULL = [r.replace(' ', 'l') if 0 < i < 2 else r for i, r in enumerate(CART)]
FULL[1] = 'kwllllllllllllwk'
add('S_CART_FULL', spr([r[:8] for r in FULL]))
add(None, spr([r[8:] for r in FULL]))

add('S_SHARD', spr(['        ', '  kk    ', ' kwwk   ', ' kwk    ', '  k     ', '        ', '        ',
                    '        ']))
add('S_SHARD2', spr(['        ', '   k    ', '  kwk   ', '  kwwk  ', '   kk   ', '        ', '        ',
                     '        ']))

DIGITS = {
    '0': ['www', 'wkw', 'wkw', 'wkw', 'www'], '1': [' w ', 'ww ', ' w ', ' w ', 'www'],
    '2': ['www', '  w', 'www', 'w  ', 'www'], '3': ['www', '  w', ' ww', '  w', 'www'],
    '4': ['w w', 'w w', 'www', '  w', '  w'], '5': ['www', 'w  ', 'www', '  w', 'www'],
    '6': ['www', 'w  ', 'www', 'w w', 'www'], '7': ['www', '  w', ' w ', ' w ', ' w '],
    '8': ['www', 'w w', 'www', 'w w', 'www'], '9': ['www', 'w w', 'www', '  w', 'www'],
    '+': ['   ', ' w ', 'www', ' w ', '   '], 'X': ['w w', 'w w', ' w ', 'w w', 'w w'],
}


def small(rows):
    """A small character (3 x 5) with a black outline, in a sprite tile."""
    grid = [[' '] * 8 for _ in range(8)]
    for y, r in enumerate(rows):
        for x, ch in enumerate(r):
            if ch == 'w':
                grid[y + 1][x + 1] = 'w'
    for y in range(8):
        for x in range(8):
            if grid[y][x] == ' ' and any(0 <= y + dy < 8 and 0 <= x + dx < 8 and grid[y + dy][x + dx] == 'w'
                                         for dy in (-1, 0, 1) for dx in (-1, 0, 1)):
                grid[y][x] = 'k'
    return spr(grid)


for i, ch in enumerate('0123456789+X'):
    add('S_DIGIT' if i == 0 else None, small(DIGITS[ch]))
add('S_ARROW_D', spr(['        ', '        ', 'kkkkk   ', 'kwwwk   ', ' kwk    ', '  k     ', '        ',
                      '        ']))

assert len(tiles) <= 256, len(tiles)

# ---------------------------------------------------------------- output

os.makedirs(OUT, exist_ok=True)


def carray(name, data, per=16):
    s = 'const uint8_t %s[%d] = {\n' % (name, len(data))
    for i in range(0, len(data), per):
        s += '    ' + ','.join('0x%02x' % v for v in data[i:i + per]) + ',\n'
    return s + '};\n'


with open(os.path.join(OUT, 'gfx.c'), 'w') as f:
    f.write('/* generated by tools/gfx.py */\n#include <stdint.h>\n#include "gfx.h"\n\n')
    f.write(carray('gfx_tiles', sum((encode(t) for t in tiles), [])))
    for n, ts in extra:
        f.write(carray(n, sum((encode(t) for t in ts), [])))
with open(os.path.join(OUT, 'gfx.h'), 'w') as f:
    f.write('/* generated by tools/gfx.py */\n#ifndef GFX_H\n#define GFX_H\n#include <stdint.h>\n\n')
    f.write('#define GFX_TILES %d\n' % len(tiles))
    for n, v in defines:
        f.write('#define %s %d\n' % (n, v))
    for n, i in names:
        f.write('#define %s %d\n' % (n, i))
    f.write('\nextern const uint8_t gfx_tiles[];\n')
    for n, ts in extra:
        f.write('extern const uint8_t %s[%d];\n' % (n, len(ts) * 16))
    f.write('\n#endif\n')

if PREVIEW:
    pal = [(224, 248, 208), (136, 192, 112), (52, 104, 86), (8, 24, 32)]
    cols = 16
    im = Image.new('RGB', (cols * 9, ((len(tiles) + cols - 1) // cols) * 9), (255, 0, 255))
    for i, t in enumerate(tiles):
        for k, v in enumerate(t):
            im.putpixel(((i % cols) * 9 + k % 8, (i // cols) * 9 + k // 8), pal[v])
    im = im.resize((im.width * 4, im.height * 4), Image.NEAREST)
    im.save(PREVIEW)
if KINDS_DIR:
    # the tile kinds at 4x, in the palette of the web page
    pal = [(0xf2, 0xed, 0xe1), (0xb9, 0xb2, 0xa2), (0x6b, 0x65, 0x58), (0x23, 0x21, 0x1d)]
    os.makedirs(KINDS_DIR, exist_ok=True)
    for k in range(1, KINDS + 1):
        rows = kind_pixels(k)
        im = Image.new('RGB', (16, 8))
        for y in range(8):
            for x in range(16):
                im.putpixel((x, y), pal[SHADE[rows[y][x]]])
        im.resize((64, 32), Image.NEAREST).save(os.path.join(KINDS_DIR, 'tile%d.png' % k), optimize=True)
print('%d tiles' % len(tiles))
