/*
 * favicon.js - the belt of the page header as the tab icon: three lanes, tiles sliding down.
 * Drawn into a 64 x 64 canvas in the colors of the page and set as the icon a few times a
 * second; favicon.png is the still picture for browsers without scripts. No motion when the
 * reader asks for less of it.
 */
'use strict';

(() => {
    const SIZE = 64, BAR = 6, LANE = 20, TILE_W = 16, TILE_H = 8, FPS = 12;
    const TRACK = SIZE - 2 * BAR + TILE_H;                  // the way of a tile, top to bottom
    // lane, kind (img/tile<kind>.png), start offset on the track
    const TILES = [[0, 1, 0], [1, 4, 22], [2, 2, 40], [0, 5, 34], [1, 7, 4], [2, 3, 12]];

    const link = document.querySelector('link[rel="icon"]');
    if (!link || matchMedia('(prefers-reduced-motion: reduce)').matches)
        return;
    const canvas = document.createElement('canvas');
    canvas.width = canvas.height = SIZE;
    const ctx = canvas.getContext('2d');
    ctx.imageSmoothingEnabled = false;

    const css = name => getComputedStyle(document.documentElement).getPropertyValue(name).trim();
    const images = {};
    let left = 0;
    for (const [, kind] of TILES) {
        if (images[kind])
            continue;
        const im = new Image();
        im.onload = () => { if (--left === 0) start(); };
        im.src = 'img/tile' + kind + '.png';
        images[kind] = im;
        left++;
    }

    function bar(y) {
        for (let x = 0; x < SIZE; x += 8) {
            ctx.fillStyle = css('--cabinet-edge');
            ctx.fillRect(x, y, 4, BAR);
            ctx.fillStyle = css('--cabinet-2');
            ctx.fillRect(x + 4, y, 4, BAR);
        }
    }

    function draw(t) {
        ctx.fillStyle = css('--cabinet');
        ctx.fillRect(0, 0, SIZE, SIZE);
        ctx.fillStyle = css('--steel');
        for (let i = 0; i < 3; i++)
            ctx.fillRect(1 + i * (LANE + 1), 0, LANE, SIZE);
        for (const [lane, kind, start] of TILES) {
            const y = BAR - TILE_H + (start + t) % TRACK;
            ctx.drawImage(images[kind], 1 + lane * (LANE + 1) + (LANE - TILE_W) / 2, y, TILE_W, TILE_H);
        }
        bar(0);
        bar(SIZE - BAR);
        ctx.fillStyle = css('--ink');
        ctx.fillRect(0, BAR, SIZE, 1);
        ctx.fillRect(0, SIZE - BAR - 1, SIZE, 1);
        link.type = 'image/png';
        link.href = canvas.toDataURL('image/png');
    }

    function start() {
        let t = 0;
        draw(t);
        setInterval(() => draw(++t), 1000 / FPS);
    }
})();
