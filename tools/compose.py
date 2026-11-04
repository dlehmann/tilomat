#!/usr/bin/env python3
"""
compose.py - the ten songs of Tilomat, written for the Game Boy's sound chip.

Each song has its own key, tempo, chords, bass line, drum pattern and tune: 32 bars in the form
A A B A. A part is 8 bars of a chord progression (scale degrees); the B part plays the same
chords from the middle. The tune is a two bar rhythm whose notes climb and fall on the tones of
the chord below it (a seeded random walk, so the result is fixed), and the last phrase of a
part comes home to the root. Channel 2 plays a third below the tune, its echo, the tune an
octave lower, or the chord as an arpeggio.

Output: tools/songs.json (read by tools/music.py): per song its name, the tempo as MUS_STEP_FP
(1/256 frames per 16th), the sound of the pulse voices, and per 16th lead, harm and bass as a
MIDI note where a note starts, 1 where it ends, 0 while it holds; drum 1 kick, 2 clap, 3 closed
and 4 open hi-hat.

Usage: tools/compose.py
"""
import json
import os
import random

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'songs.json')
MAJOR, MINOR = [0, 2, 4, 5, 7, 9, 11], [0, 2, 3, 5, 7, 8, 10]
FPS = 59.7275
BARS = 32
NS = BARS * 16


def step_fp(bpm):
    return int(round(60 / bpm / 4 * FPS * 256))


def chord(key, scale, degree):
    """The triad on a scale degree (1..7) as pitch classes, root first."""
    return [(key + scale[(degree - 1 + i) % 7]) % 12 for i in (0, 2, 4)]


def ladder(pcs, lo, hi):
    return [n for n in range(lo, hi + 1) if n % 12 in pcs]


def render(notes):
    """(start, length, note) to events."""
    ev = [0] * NS
    for st, ln, p in notes:
        ev[st % NS] = p
    for st, ln, p in notes:
        if ev[(st + ln) % NS] == 0:
            ev[(st + ln) % NS] = 1
    return ev


# 16 steps: 1 kick, 2 clap, 3 closed hat, 4 open hat
DRUMS = {
    'four':    [1, 0, 3, 0, 2, 0, 3, 0, 1, 0, 3, 0, 2, 0, 3, 4],
    'disco':   [1, 0, 4, 0, 1, 0, 4, 0, 1, 0, 4, 0, 1, 0, 4, 0],
    'rock':    [1, 0, 3, 0, 2, 0, 3, 0, 1, 0, 1, 0, 2, 0, 3, 0],
    'break':   [1, 0, 3, 0, 2, 0, 3, 1, 0, 1, 3, 0, 2, 0, 3, 0],
    'half':    [1, 0, 3, 0, 3, 0, 3, 0, 2, 0, 3, 0, 3, 0, 3, 0],
    'gallop':  [1, 0, 3, 3, 2, 0, 3, 3, 1, 0, 3, 3, 2, 0, 3, 3],
    'shuffle': [1, 0, 0, 3, 2, 0, 0, 3, 1, 0, 0, 3, 2, 0, 0, 3],
}


def bass_bar(style, root, third, fifth, nxt):
    """One bar of bass as (step, length, note)."""
    if style == 'eighths':
        return [(s, 2, root) for s in range(0, 16, 2)]
    if style == 'octave':
        return [(s, 2, root + (12 if s % 4 else 0)) for s in range(0, 16, 2)]
    if style == 'walk':
        return [(0, 4, root), (4, 4, third), (8, 4, fifth), (12, 4, nxt - 1 if nxt > root else nxt + 1)]
    if style == 'synco':
        return [(0, 3, root), (3, 3, root), (6, 2, fifth), (10, 2, root + 12), (12, 4, fifth)]
    if style == 'long':
        return [(0, 8, root), (8, 6, fifth), (14, 2, root)]
    if style == 'gallop':
        return [(s + o, 1, root) for s in range(0, 16, 4) for o in (0, 2, 3)]
    raise KeyError(style)


def song(name, seed, key, minor, bpm, prog, bass, drums, rhythm_a, rhythm_b, lo, hi, harm, sound, intro=None):
    rnd = random.Random(seed)
    scale = MINOR if minor else MAJOR
    form = [('A', prog), ('A2', prog), ('B', prog[4:] + prog[:4]), ('A', prog)]
    lead, second, low, drum = [], [], [], []
    motifs = {}

    def contour(rhythm, home):
        c, k = [], rnd.choice([0, 1, 2])
        for _ in rhythm:
            k = max(-2, min(5, k + rnd.choice([-1, -1, 0, 1, 1, 2, -2])))
            c.append(k)
        if home:
            c[-1] = 0
        return c

    for part, (kind, bars) in enumerate(form):
        rhythm = rhythm_b if kind == 'B' else rhythm_a
        m = 'B' if kind == 'B' else 'A'
        if m not in motifs:
            motifs[m] = (contour(rhythm, False), contour(rhythm, True))
        for b, deg in enumerate(bars):
            s0 = (part * 8 + b) * 16
            pcs = chord(key, scale, deg)
            root = 36 + (pcs[0] - 36) % 12
            nxt = 36 + (chord(key, scale, bars[(b + 1) % 8])[0] - 36) % 12
            low += [(s0 + s, n, p) for s, n, p in bass_bar(bass, root, root + (pcs[1] - pcs[0]) % 12,
                                                             root + (pcs[2] - pcs[0]) % 12, nxt)]
            drum += DRUMS[intro if intro and part == 0 and b < 4 else drums]
            if b % 2:
                continue
            c = motifs[m][1 if b == 6 else 0]
            if kind == 'A2' and b in (2, 6):
                c = [x + 1 for x in c[:-1]] + [c[-1]]
            ladders = ladder(pcs, lo, hi), ladder(chord(key, scale, bars[b + 1]), lo, hi)
            for (st, ln), k in zip(rhythm, c):
                lad = ladders[st >= 16]
                i0 = lad.index(min(lad, key=lambda n: abs(n - (lo + hi) // 2)))
                p = lad[max(0, min(len(lad) - 1, i0 + k))]
                lead.append((s0 + st, ln, p))
                if harm == 'third':
                    j = lad.index(p)
                    second.append((s0 + st, ln, lad[j - 1] if j else p - 12))
                elif harm == 'oct':
                    second.append((s0 + st, ln, p - 12))
    tune = render(lead)
    if harm == 'echo':
        h = [tune[(s - 3) % NS] for s in range(NS)]
    elif harm == 'arp':
        h = []
        for s in range(NS):
            part, b = divmod(s // 16, 8)
            lad = ladder(chord(key, scale, form[part][1][b]), lo - 12, lo + 7)
            h.append(lad[[0, 1, 2, 1][s % 4]] if s % 2 == 0 else 0)
    elif harm == 'none':
        h = [0] * NS
    else:
        h = render(second)
    return dict(name=name, bpm=bpm, step_fp=step_fp(bpm), lead=tune, harm=h, bass=render(low), drum=drum, **sound)


# two bar rhythms of the tune: (step, length)
HOOK = [(0, 2), (3, 2), (6, 2), (8, 3), (12, 2), (14, 2), (16, 2), (19, 2), (22, 3), (26, 2), (28, 4)]
CALM = [(0, 6), (6, 2), (8, 8), (16, 4), (20, 4), (24, 8)]
RUN = [(i, 1) for i in range(32) if i % 8 != 7]
FUNK = [(2, 1), (4, 2), (7, 1), (10, 2), (14, 2), (18, 1), (20, 2), (23, 1), (26, 2), (30, 2)]
POP = [(0, 2), (2, 2), (4, 4), (8, 2), (10, 2), (12, 4), (16, 3), (19, 3), (22, 2), (24, 8)]
STAC = [(0, 1), (2, 1), (3, 1), (6, 1), (8, 1), (10, 2), (14, 1), (16, 1), (18, 1), (19, 1), (22, 1), (24, 4),
        (30, 1)]
GALLOP = [(0, 1), (2, 1), (3, 1), (4, 2), (8, 1), (10, 1), (11, 1), (12, 4), (16, 1), (18, 1), (19, 1), (20, 2),
          (24, 8)]
SWING = [(0, 3), (3, 1), (4, 3), (7, 1), (8, 4), (12, 4), (16, 3), (19, 1), (20, 3), (23, 1), (24, 8)]

# duty (NRx1) and envelope (NRx2) of the tune; channel 2 keeps a 25 % pulse at volume 7
SOFT = dict(lead_duty=0x40, lead_env=0xa7)
BRIGHT = dict(lead_duty=0x80, lead_env=0xc4)
PLUCK = dict(lead_duty=0x80, lead_env=0xd2)

C, D, E, F, G, A = 0, 2, 4, 5, 7, 9
SONGS = [
    song('CONVEYOR BOOGIE', 1, C, False, 140, [1, 6, 4, 5, 1, 6, 2, 5], 'octave', 'four', HOOK, POP, 67, 86, 'third',
         BRIGHT, 'half'),
    song('NIGHT SHIFT', 2, A, True, 112, [1, 6, 3, 7, 1, 6, 4, 5], 'long', 'half', CALM, SWING, 64, 84, 'echo', SOFT),
    song('TURBO BEAT', 3, E, True, 160, [1, 7, 6, 7, 1, 7, 6, 5], 'eighths', 'rock', RUN, STAC, 64, 88, 'none', PLUCK),
    song('PUZZLE POP', 4, G, False, 128, [1, 5, 6, 4, 1, 5, 6, 4], 'synco', 'four', POP, HOOK, 67, 86, 'third', BRIGHT,
         'half'),
    song('RED LINE', 5, D, True, 150, [1, 4, 5, 1, 1, 4, 6, 5], 'eighths', 'break', STAC, GALLOP, 62, 84, 'oct', PLUCK),
    song('SUN DECK', 6, D, False, 116, [1, 4, 2, 5, 1, 4, 2, 5], 'synco', 'disco', FUNK, POP, 66, 86, 'arp', BRIGHT),
    song('CRYSTAL', 7, E, False, 132, [1, 6, 2, 5, 1, 6, 4, 5], 'walk', 'four', POP, CALM, 68, 88, 'arp', SOFT),
    song('FINAL SPRINT', 8, C, True, 172, [1, 6, 7, 1, 1, 6, 4, 5], 'gallop', 'gallop', GALLOP, RUN, 60, 84, 'third',
         PLUCK),
    song('WAY HOME', 9, G, False, 96, [1, 6, 4, 5, 1, 3, 4, 5], 'long', 'half', CALM, POP, 67, 86, 'third', SOFT),
    song('TILE SHUFFLE', 10, F, False, 124, [1, 4, 1, 5, 1, 4, 5, 1], 'walk', 'shuffle', SWING, HOOK, 65, 86, 'echo',
         BRIGHT),
]

with open(OUT, 'w') as f:
    json.dump(SONGS, f, separators=(',', ':'))
print('%d songs' % len(SONGS))
