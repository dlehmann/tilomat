#!/usr/bin/env python3
"""
run.py - plays Tilomat in PyBoy without a window, by a script, and takes screenshots.

Script items (as arguments): wait:N (frames), press:KEY[:frames] (a, b, start, select, up, down,
left, right), shot:FILE (PNG, scaled up 3x), sound:FILE (the sound so far as WAV)

Usage: tools/run.py <rom> item...
"""
import sys
import wave

from pyboy import PyBoy

rom = sys.argv[1]
pyboy = PyBoy(rom, window='null', sound_emulated=True)
pcm = []

for item in sys.argv[2:]:
    kind, _, arg = item.partition(':')
    if kind == 'wait':
        for _ in range(int(arg)):
            pyboy.tick(1, True, True)
            if '--sound' in sys.argv[2:] or any(a.startswith('sound:') for a in sys.argv[2:]):
                pcm.append(bytes(pyboy.sound.ndarray.tobytes()))
    elif kind == 'press':
        key, _, n = arg.partition(':')
        pyboy.button(key, int(n) if n else 2)
        pyboy.tick(1, True, True)
    elif kind == 'shot':
        im = pyboy.screen.image.convert('RGB')
        im = im.resize((im.width * 3, im.height * 3), 0)
        im.save(arg)
    elif kind == 'sound':
        with wave.open(arg, 'wb') as w:
            w.setnchannels(2)
            w.setsampwidth(1)
            w.setframerate(pyboy.sound.sample_rate)
            w.writeframes(b''.join(pcm))
pyboy.stop(save=False)
