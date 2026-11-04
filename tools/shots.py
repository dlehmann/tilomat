#!/usr/bin/env python3
"""
shots.py - the screenshots and the animation of Tilomat for the README and the web page, in
the warm grays of the web page, at twice the size.

It plays the ROM in PyBoy: the title screens by a script of button presses, the game by
tools/autoplay.py (a good player, and a sloppy one for the game over, the name entry and the
high score list). Both are deterministic, so the frames below always show the same. The game
runs in English; only title.png shows the German title screen.

Output: docs/img/*.png (screenshots.png: the strip for the README), docs/img/gameplay.gif

Usage: tools/shots.py <rom>
"""
import os
import subprocess
import sys
import tempfile

from PIL import Image
from pyboy import PyBoy

ROM = sys.argv[1] if len(sys.argv) > 1 else 'build/tilomat.gb'
OUT = 'docs/img'
PY = sys.executable
SCALE = 2
# the shades of PyBoy -> the palette of the web page
PAL = {(255, 255, 255): (0xf2, 0xed, 0xe1), (153, 153, 153): (0xb9, 0xb2, 0xa2),
       (85, 85, 85): (0x6b, 0x65, 0x58), (0, 0, 0): (0x23, 0x21, 0x1d)}

# the good player: name -> frame
GOOD = {'help': 20, 'play': 2100, 'done': 2600, 'intro': 58130, 'play2': 64000, 'play3': 68000}
GIF = (54400, 55000)
# the sloppy player (700 of 1000 decisions wrong)
BAD = {'gameover': 2300}
# the strip for the README
STRIP = ('title-en', 'help', 'play2', 'music')
GAP = 12
TAIL = 'wait:30,shot:entry,press:up,wait:4,press:up,wait:30,press:start,wait:60,shot:scores,wait:400,shot:title-record'


def tint(im):
    im = im.convert('RGB')
    px = im.load()
    for y in range(im.height):
        for x in range(im.width):
            px[x, y] = PAL.get(px[x, y], px[x, y])
    return im.resize((im.width * SCALE, im.height * SCALE), Image.NEAREST)


def save(im, name):
    tint(im).save(os.path.join(OUT, name + '.png'), optimize=True)
    print(name)


def titles():
    """The title screen in German and English."""
    p = PyBoy(ROM, window='null')
    p.tick(150, True)
    save(p.screen.image, 'title')
    p.button('select', 2)
    p.tick(40, True)
    save(p.screen.image, 'title-en')
    p.stop(save=False)


def music():
    """The music menu, in English, with the third song chosen."""
    p = PyBoy(ROM, window='null')
    p.tick(150, True)
    for key, wait in (('select', 40), ('start', 60), ('a', 60), ('select', 20), ('down', 10), ('down', 30)):
        p.button(key, 2)
        p.tick(wait, True)
    save(p.screen.image, 'music')
    p.stop(save=False)


def autoplay(frames, mistakes, shots, gif=None, tail=None):
    with tempfile.TemporaryDirectory() as tmp:
        env = dict(os.environ, ENGLISH='1', EVERY='0', AT=','.join(str(f) for f in shots.values()))
        if gif:
            env['GIF'] = '%d:%d' % gif
        if tail:
            env['TAIL'] = tail
        subprocess.run([PY, 'tools/autoplay.py', ROM, tmp, str(frames), str(mistakes)], env=env, check=True,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        for name, f in shots.items():
            save(Image.open(os.path.join(tmp, 'f%06d.png' % f)), name)
        if tail:
            for item in tail.split(','):
                if item.startswith('shot:'):
                    name = item[5:]
                    save(Image.open(os.path.join(tmp, name + '.png')), name)
        if gif:
            frames = [tint(Image.open(os.path.join(tmp, 'g%06d.png' % f))) for f in range(gif[0], gif[1], 2)]
            pal = frames[0].quantize(colors=4)
            frames = [fr.quantize(palette=pal) for fr in frames]
            frames[0].save(os.path.join(OUT, 'gameplay.gif'), save_all=True, append_images=frames[1:],
                           duration=33, loop=0, optimize=True)
            print('gameplay.gif')


def strip():
    ims = [Image.open(os.path.join(OUT, name + '.png')) for name in STRIP]
    w, h = ims[0].size
    out = Image.new('RGB', (len(ims) * (w + GAP) - GAP, h), PAL[(255, 255, 255)])
    for i, im in enumerate(ims):
        out.paste(im, (i * (w + GAP), 0))
    out.save(os.path.join(OUT, 'screenshots.png'), optimize=True)
    print('screenshots')


os.makedirs(OUT, exist_ok=True)
titles()
music()
autoplay(max(GOOD.values()) + 1, 0, GOOD, GIF)
autoplay(12000, 700, BAD, tail=TAIL)
strip()
