# Tilomat - builds build/tilomat.gb
LCC = tools/gbdk/bin/lcc
PY = tools/venv/bin/python

GEN = gen/gfx.c gen/text.c gen/music.c
CSRC = $(wildcard src/*.c)
HDR = $(wildcard src/*.h) gen/gfx.h gen/text.h gen/music.h
# MBC1 with battery-backed RAM (8 KB) for the high score list
CFLAGS = -Isrc -Igen -Wa-l -Wl-m -Wl-yt0x03 -Wl-ya1 -Wm-yn"TILOMAT"

all: build/tilomat.gb

gen/gfx.c gen/gfx.h &: tools/gfx.py
	mkdir -p build
	$(PY) tools/gfx.py gen build/tiles.png

gen/text.c gen/text.h &: tools/text.py gen/gfx.h
	$(PY) tools/text.py gen

gen/music.c gen/music.h &: tools/music.py
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
