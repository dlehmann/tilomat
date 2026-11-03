#!/usr/bin/env python3
"""
autoplay.py - plays Tilomat in PyBoy by itself, for tests and screenshots: it reads the game
state from the RAM (the address of `g` from build/tilomat.map), catches the nearest tile and
drops its tiles where they fit best, and takes screenshots now and then.

Environment: EVERY (screenshot every n frames, default 600), AT (frames, comma separated),
GIF=start:end (every frame as g<frame>.png), SAVE=1 keeps the cartridge RAM in <rom>.ram,
TAIL=items after the game (comma separated, as in tools/run.py: wait:N, press:KEY, shot:NAME),
ENGLISH=1 plays in English.

Usage: tools/autoplay.py <rom> <out dir> [frames] [mistakes per 1000 decisions]
"""
import os
import random
import re
import sys

from pyboy import PyBoy

ROM = sys.argv[1]
OUT = sys.argv[2]
FRAMES = int(sys.argv[3]) if len(sys.argv) > 3 else 20000
MISTAKES = int(sys.argv[4]) if len(sys.argv) > 4 else 0
EVERY = int(os.environ.get('EVERY', 600))
AT = {int(v) for v in os.environ.get('AT', '').split(',') if v}
GIF = [int(v) for v in os.environ.get('GIF', '0:0').split(':')]
LANES, SLOTS, ROWS, CART_MAX, JOKER = 5, 9, 5, 4, 7
os.makedirs(OUT, exist_ok=True)

# the fields of Game (src/game.h) in order, with their sizes; SDCC does not pad
FIELDS = [('rng', 2), ('phase', 1), ('timer', 2), ('done', 1), ('shift', 1), ('score', 4), ('shift_points', 4),
          ('goal', 1), ('goal_kind', 1), ('target', 2), ('count', 2), ('breaks', 1), ('kinds', 1), ('joker', 1),
          ('new_kind', 1), ('step_ticks', 1), ('step_pos', 1), ('half', 1), ('steps', 1), ('spawn_wait', 1),
          ('last_lane', 1), ('bag', 12), ('bag_n', 1), ('belt', LANES * SLOTS), ('cart', 1), ('stack', CART_MAX),
          ('stack_n', 1), ('bin', LANES * ROWS), ('marked', LANES * ROWS), ('fall', LANES * ROWS),
          ('clearing', 1)]
OFF = {}
o = 0
for name, size in FIELDS:
    OFF[name] = (o, size)
    o += size

sym = {}
for line in open(os.path.join(os.path.dirname(ROM), 'tilomat.map')):
    for m in re.finditer(r'([0-9A-F]{8})\s+(_\w+)', line):
        sym[m.group(2)] = int(m.group(1), 16)
G = sym['_g']


pyboy = PyBoy(ROM, window='null')
mem = pyboy.memory
rng = random.Random(1)


def field(name):
    off, size = OFF[name]
    data = bytes(mem[G + off:G + off + size])
    return int.from_bytes(data, 'little') if size in (2, 4) and name not in ('bag', 'stack') else list(data) \
        if size > 1 else data[0]


def column_score(binv, col, kind):
    """How well a tile of `kind` fits on top of column col (higher is better)."""
    h = 0
    while h < ROWS and binv[col * ROWS + h]:
        h += 1
    if h >= ROWS:
        return -100
    s = 0
    for dc in (-1, 0, 1):
        for dr in (-1, 0, 1):
            c, r = col + dc, h + dr
            if (dc or dr) and 0 <= c < LANES and 0 <= r < ROWS:
                v = binv[c * ROWS + r]
                if v and (v == kind or v == JOKER or kind == JOKER):
                    s += 4 if dc == 0 or dr == 0 else 3
                    # two in a line: completing a line
                    c2, r2 = c + dc, r + dr
                    if 0 <= c2 < LANES and 0 <= r2 < ROWS and binv[c2 * ROWS + r2] in (kind, JOKER):
                        s += 10
                    c3, r3 = col - dc, h - dr
                    if 0 <= c3 < LANES and 0 <= r3 < ROWS and binv[c3 * ROWS + r3] in (kind, JOKER):
                        s += 10
    return s - 2 * h


def decide():
    """The button to press now, or None."""
    if field('phase') != 1:
        return 'a' if field('timer') % 60 == 59 else None
    if rng.randrange(1000) < MISTAKES:
        return rng.choice([None, None, 'left', 'right', 'a'])
    belt = field('belt')
    cart = field('cart')
    stack_n = field('stack_n')
    nearest, lane_n = 99, None
    for lane in range(LANES):
        for s in range(SLOTS):
            if belt[lane * SLOTS + s] and s < nearest:
                nearest, lane_n = s, lane
    if stack_n and (stack_n == CART_MAX or nearest > 3 or lane_n == cart and nearest > 1):
        kind = field('stack')[stack_n - 1] & 15
        binv = field('bin')
        best, bv = cart, -1000
        for c in range(LANES):
            v = column_score(binv, c, kind) - abs(c - cart)
            if v > bv:
                best, bv = c, v
        if bv <= -90:
            return 'b'
        if best != cart:
            return 'left' if best < cart else 'right'
        return 'a'
    if lane_n is not None and lane_n != cart:
        return 'left' if lane_n < cart else 'right'
    return None


pyboy.tick(120, True)
if os.environ.get('ENGLISH'):
    mem[sym['_lang']] = 1
pyboy.button('start', 2)
pyboy.tick(30, True)
shift = 0
play_frames = play_ticks = 0
for f in range(FRAMES):
    t0 = field('timer')
    if f % 4 == 0:
        b = decide()
        if b:
            pyboy.button(b, 2)
    pyboy.tick(1, True)
    # the game speed: ticks per frame while playing (frames with a phase change are left out)
    if field('phase') == 1 and field('timer') >= t0:
        play_frames += 1
        play_ticks += field('timer') - t0
    s = field('shift')
    if s != shift:
        shift = s
        print('frame %d: shift %d, score %d, breaks %d' % (f, s, field('score'), field('breaks')))
    if (EVERY and f % EVERY == 0) or f in AT:
        pyboy.screen.image.convert('RGB').save(os.path.join(OUT, 'f%06d.png' % f))
    if GIF[0] <= f < GIF[1]:
        pyboy.screen.image.convert('RGB').save(os.path.join(OUT, 'g%06d.png' % f))
    if field('done'):
        print('frame %d: game over, score %d' % (f, field('score')))
        break
if play_frames:
    print('speed while playing: %d ticks in %d frames (%.1f %%)' % (play_ticks, play_frames,
                                                                  100.0 * play_ticks / play_frames))
for item in [v for v in os.environ.get('TAIL', '').split(',') if v]:
    kind, _, arg = item.partition(':')
    if kind == 'wait':
        pyboy.tick(int(arg), True)
    elif kind == 'press':
        pyboy.button(arg, 2)
        pyboy.tick(1, True)
    elif kind == 'shot':
        pyboy.screen.image.convert('RGB').save(os.path.join(OUT, arg + '.png'))
pyboy.stop(save=os.environ.get('SAVE') == '1')
