# Tilomat - builds build/tilomat.gb
LCC = tools/gbdk/bin/lcc
PY = tools/venv/bin/python

GEN = gen/gfx.c gen/text.c gen/music.c gen/songs2.c gen/songs3.c
CSRC = $(wildcard src/*.c)
HDR = $(wildcard src/*.h) gen/gfx.h gen/text.h gen/music.h
# MBC1 with battery-backed RAM (8 KB) for the high score list, 64 KB of ROM (the songs in the
# banks 2 and 3); _HOME (the NONBANKED code) first, so it stays in bank 0
CFLAGS = -Isrc -Igen -Wa-l -Wl-m -Wl-yt0x03 -Wl-yo4 -Wl-ya1 -Wm-yn"TILOMAT" -Wl-b_HOME=0x200 -Wl-b_CODE=0x400

all: build/tilomat.gb

gen/gfx.c gen/gfx.h &: tools/gfx.py
	mkdir -p build
	$(PY) tools/gfx.py gen build/tiles.png

gen/text.c gen/text.h &: tools/text.py gen/gfx.h tools/songs.json
	$(PY) tools/text.py gen

gen/music.c gen/music.h gen/songs2.c gen/songs3.c &: tools/music.py tools/songs.json
	$(PY) tools/music.py gen

build/tilomat.gb: $(CSRC) $(GEN) $(HDR)
	mkdir -p build
	$(LCC) $(CFLAGS) -o $@ $(CSRC) $(GEN)
	cp $@ tilomat.gb

test: tools/test_game.c src/game.c src/game.h
	mkdir -p build
	cc -std=c99 -Wall -Wextra -Isrc -o build/test_game tools/test_game.c src/game.c
	build/test_game

clean:
	rm -rf build gen tilomat.gb

.PHONY: all clean test
