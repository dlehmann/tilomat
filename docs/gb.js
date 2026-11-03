/*
 * gb.js - a small emulator of the original Game Boy (DMG), written to play Polonaise and now
 * Tilomat in the browser: the SM83 CPU, the picture processor (background, window,
 * sprites), the timer, the joypad, the sound chip and an MBC1 cartridge with RAM.
 *
 * It aims at what games like this one need, not at passing every test ROM: the picture is
 * drawn a line at a time, the CPU counts cycles per instruction.
 *
 * Usage:
 *   const gb = new GameBoy(romBytes, { sampleRate: 48000 });
 *   gb.runFrame();               // about 1/60 s; gb.frame is 160 x 144 shades (0..3)
 *   gb.setButton('a', true);     // right left up down a b select start
 *   gb.takeAudio();              // stereo samples produced so far (Float32Array, interleaved)
 *   gb.sram / gb.onSave          // the cartridge RAM, and a callback when the game saves
 */
'use strict';

const CLOCK = 4194304;
const FRAME_CYCLES = 70224;

/* machine cycles of the instructions (conditional ones: not taken) */
const CYCLES = [
    1, 3, 2, 2, 1, 1, 2, 1, 5, 2, 2, 2, 1, 1, 2, 1,
    1, 3, 2, 2, 1, 1, 2, 1, 3, 2, 2, 2, 1, 1, 2, 1,
    2, 3, 2, 2, 1, 1, 2, 1, 2, 2, 2, 2, 1, 1, 2, 1,
    2, 3, 2, 2, 3, 3, 3, 1, 2, 2, 2, 2, 1, 1, 2, 1,
    1, 1, 1, 1, 1, 1, 2, 1, 1, 1, 1, 1, 1, 1, 2, 1,
    1, 1, 1, 1, 1, 1, 2, 1, 1, 1, 1, 1, 1, 1, 2, 1,
    1, 1, 1, 1, 1, 1, 2, 1, 1, 1, 1, 1, 1, 1, 2, 1,
    2, 2, 2, 2, 2, 2, 1, 2, 1, 1, 1, 1, 1, 1, 2, 1,
    1, 1, 1, 1, 1, 1, 2, 1, 1, 1, 1, 1, 1, 1, 2, 1,
    1, 1, 1, 1, 1, 1, 2, 1, 1, 1, 1, 1, 1, 1, 2, 1,
    1, 1, 1, 1, 1, 1, 2, 1, 1, 1, 1, 1, 1, 1, 2, 1,
    1, 1, 1, 1, 1, 1, 2, 1, 1, 1, 1, 1, 1, 1, 2, 1,
    2, 3, 3, 4, 3, 4, 2, 4, 2, 4, 3, 1, 3, 6, 2, 4,
    2, 3, 3, 1, 3, 4, 2, 4, 2, 4, 3, 1, 3, 1, 2, 4,
    3, 3, 2, 1, 1, 4, 2, 4, 4, 1, 4, 1, 1, 1, 2, 4,
    3, 3, 2, 1, 1, 4, 2, 4, 3, 2, 4, 1, 1, 1, 2, 4,
];

const BUTTONS = { right: 0, left: 1, up: 2, down: 3, a: 4, b: 5, select: 6, start: 7 };
const DUTY = [[0, 0, 0, 0, 0, 0, 0, 1], [1, 0, 0, 0, 0, 0, 0, 1], [1, 0, 0, 0, 0, 1, 1, 1], [0, 1, 1, 1, 1, 1, 1, 0]];
const NOISE_DIV = [8, 16, 32, 48, 64, 80, 96, 112];
/* bits that read as 1 in the sound registers FF10..FF26 */
const APU_MASK = [0x80, 0x3f, 0x00, 0xff, 0xbf, 0xff, 0x3f, 0x00, 0xff, 0xbf, 0x7f, 0xff, 0x9f, 0xff, 0xbf, 0xff,
    0xff, 0x00, 0x00, 0xbf, 0x00, 0x00, 0x70];

/* ------------------------------------------------------------------ sound */

class Square {
    constructor(sweep) {
        this.hasSweep = sweep;
        this.on = false; this.dac = false;
        this.duty = 0; this.pos = 0; this.freq = 0; this.timer = 0;
        this.length = 0; this.lengthOn = false;
        this.vol0 = 0; this.envUp = false; this.envPeriod = 0; this.vol = 0; this.envTimer = 0;
        this.sweepPeriod = 0; this.sweepNeg = false; this.sweepShift = 0; this.sweepTimer = 0; this.shadow = 0;
        this.sweepOn = false;
    }
    tick(c) {
        this.timer -= c;
        while (this.timer <= 0) {
            this.timer += (2048 - this.freq) * 4;
            this.pos = (this.pos + 1) & 7;
        }
    }
    out() {
        return this.on && this.dac && DUTY[this.duty][this.pos] ? this.vol : 0;
    }
    clockLength() {
        if (this.lengthOn && this.length > 0 && --this.length === 0)
            this.on = false;
    }
    clockEnvelope() {
        if (!this.envPeriod || --this.envTimer > 0)
            return;
        this.envTimer = this.envPeriod;
        if (this.envUp && this.vol < 15)
            this.vol++;
        else if (!this.envUp && this.vol > 0)
            this.vol--;
    }
    sweepCalc() {
        const d = this.shadow >> this.sweepShift;
        const f = this.sweepNeg ? this.shadow - d : this.shadow + d;
        if (f > 2047)
            this.on = false;
        return f;
    }
    clockSweep() {
        if (--this.sweepTimer > 0)
            return;
        this.sweepTimer = this.sweepPeriod || 8;
        if (!this.sweepOn || !this.sweepPeriod)
            return;
        const f = this.sweepCalc();
        if (f <= 2047 && this.sweepShift) {
            this.shadow = this.freq = f;
            this.sweepCalc();
        }
    }
    trigger() {
        this.on = this.dac;
        if (!this.length)
            this.length = 64;
        this.timer = (2048 - this.freq) * 4;
        this.vol = this.vol0;
        this.envTimer = this.envPeriod;
        if (this.hasSweep) {
            this.shadow = this.freq;
            this.sweepTimer = this.sweepPeriod || 8;
            this.sweepOn = this.sweepPeriod > 0 || this.sweepShift > 0;
            if (this.sweepShift)
                this.sweepCalc();
        }
    }
}

class Wave {
    constructor() {
        this.on = false; this.dac = false; this.freq = 0; this.timer = 0; this.pos = 0;
        this.length = 0; this.lengthOn = false; this.shift = 0;
        this.ram = new Uint8Array(16);
    }
    tick(c) {
        this.timer -= c;
        while (this.timer <= 0) {
            this.timer += (2048 - this.freq) * 2;
            this.pos = (this.pos + 1) & 31;
        }
    }
    out() {
        if (!this.on || !this.dac || !this.shift)
            return 0;
        const b = this.ram[this.pos >> 1];
        return ((this.pos & 1) ? b & 15 : b >> 4) >> (this.shift - 1);
    }
    clockLength() {
        if (this.lengthOn && this.length > 0 && --this.length === 0)
            this.on = false;
    }
    trigger() {
        this.on = this.dac;
        if (!this.length)
            this.length = 256;
        this.timer = (2048 - this.freq) * 2;
        this.pos = 0;
    }
}

class Noise {
    constructor() {
        this.on = false; this.dac = false; this.timer = 0; this.lfsr = 0x7fff;
        this.shift = 0; this.width7 = false; this.div = 0;
        this.length = 0; this.lengthOn = false;
        this.vol0 = 0; this.envUp = false; this.envPeriod = 0; this.vol = 0; this.envTimer = 0;
    }
    period() {
        return NOISE_DIV[this.div] << this.shift;
    }
    tick(c) {
        this.timer -= c;
        while (this.timer <= 0) {
            this.timer += this.period();
            const x = (this.lfsr ^ (this.lfsr >> 1)) & 1;
            this.lfsr = (this.lfsr >> 1) | (x << 14);
            if (this.width7)
                this.lfsr = (this.lfsr & ~0x40) | (x << 6);
        }
    }
    out() {
        return this.on && this.dac && !(this.lfsr & 1) ? this.vol : 0;
    }
    trigger() {
        this.on = this.dac;
        if (!this.length)
            this.length = 64;
        this.timer = this.period();
        this.lfsr = 0x7fff;
        this.vol = this.vol0;
        this.envTimer = this.envPeriod;
    }
}
Noise.prototype.clockLength = Square.prototype.clockLength;
Noise.prototype.clockEnvelope = Square.prototype.clockEnvelope;

class APU {
    constructor(sampleRate) {
        this.regs = new Uint8Array(0x30);
        this.ch1 = new Square(true);
        this.ch2 = new Square(false);
        this.ch3 = new Wave();
        this.ch4 = new Noise();
        this.power = true;
        this.seqTimer = 8192;
        this.seqStep = 0;
        this.rate = sampleRate;
        this.perSample = CLOCK / sampleRate;
        this.sampleTimer = this.perSample;
        this.buf = new Float32Array(sampleRate);     // a second at most
        this.n = 0;
        this.hp = [0, 0, 0, 0];
    }

    read(a) {
        const r = a - 0xff10;
        if (a >= 0xff30)
            return this.ch3.ram[a - 0xff30];
        if (a === 0xff26)
            return (this.power ? 0x80 : 0) | 0x70 | (this.ch1.on ? 1 : 0) | (this.ch2.on ? 2 : 0) |
                (this.ch3.on ? 4 : 0) | (this.ch4.on ? 8 : 0);
        return r < APU_MASK.length ? this.regs[r] | APU_MASK[r] : 0xff;
    }

    write(a, v) {
        if (a >= 0xff30) {
            this.ch3.ram[a - 0xff30] = v;
            return;
        }
        if (a === 0xff26) {
            this.power = !!(v & 0x80);
            if (!this.power) {
                this.regs.fill(0);
                for (const ch of [this.ch1, this.ch2, this.ch3, this.ch4])
                    ch.on = false;
            }
            return;
        }
        if (!this.power)
            return;
        this.regs[a - 0xff10] = v;
        const c1 = this.ch1, c2 = this.ch2, c3 = this.ch3, c4 = this.ch4;
        switch (a) {
        case 0xff10: c1.sweepPeriod = (v >> 4) & 7; c1.sweepNeg = !!(v & 8); c1.sweepShift = v & 7; break;
        case 0xff11: c1.duty = v >> 6; c1.length = 64 - (v & 63); break;
        case 0xff12: this.envelope(c1, v); break;
        case 0xff13: c1.freq = (c1.freq & 0x700) | v; break;
        case 0xff14: this.freqHi(c1, v); break;
        case 0xff16: c2.duty = v >> 6; c2.length = 64 - (v & 63); break;
        case 0xff17: this.envelope(c2, v); break;
        case 0xff18: c2.freq = (c2.freq & 0x700) | v; break;
        case 0xff19: this.freqHi(c2, v); break;
        case 0xff1a: c3.dac = !!(v & 0x80); if (!c3.dac) c3.on = false; break;
        case 0xff1b: c3.length = 256 - v; break;
        case 0xff1c: c3.shift = [0, 1, 2, 3][(v >> 5) & 3]; break;
        case 0xff1d: c3.freq = (c3.freq & 0x700) | v; break;
        case 0xff1e: this.freqHi(c3, v); break;
        case 0xff20: c4.length = 64 - (v & 63); break;
        case 0xff21: this.envelope(c4, v); break;
        case 0xff22: c4.shift = v >> 4; c4.width7 = !!(v & 8); c4.div = v & 7; break;
        case 0xff23: c4.lengthOn = !!(v & 0x40); if (v & 0x80) c4.trigger(); break;
        }
    }

    envelope(ch, v) {
        ch.vol0 = v >> 4;
        ch.envUp = !!(v & 8);
        ch.envPeriod = v & 7;
        ch.dac = (v & 0xf8) !== 0;
        if (!ch.dac)
            ch.on = false;
    }

    freqHi(ch, v) {
        ch.freq = (ch.freq & 0xff) | ((v & 7) << 8);
        ch.lengthOn = !!(v & 0x40);
        if (v & 0x80)
            ch.trigger();
    }

    tick(c) {
        this.ch1.tick(c);
        this.ch2.tick(c);
        this.ch3.tick(c);
        this.ch4.tick(c);
        this.seqTimer -= c;
        while (this.seqTimer <= 0) {
            this.seqTimer += 8192;
            const s = this.seqStep;
            this.seqStep = (s + 1) & 7;
            if (!(s & 1))
                for (const ch of [this.ch1, this.ch2, this.ch3, this.ch4])
                    ch.clockLength();
            if (s === 2 || s === 6)
                this.ch1.clockSweep();
            if (s === 7) {
                this.ch1.clockEnvelope();
                this.ch2.clockEnvelope();
                this.ch4.clockEnvelope();
            }
        }
        this.sampleTimer -= c;
        while (this.sampleTimer <= 0) {
            this.sampleTimer += this.perSample;
            this.sample();
        }
    }

    sample() {
        if (this.n + 2 > this.buf.length)
            return;
        let l = 0, r = 0;
        if (this.power) {
            const o = [this.ch1.out(), this.ch2.out(), this.ch3.out(), this.ch4.out()];
            const pan = this.regs[0x15], vol = this.regs[0x14];
            for (let i = 0; i < 4; i++) {
                if (pan & (0x10 << i)) l += o[i];
                if (pan & (1 << i)) r += o[i];
            }
            l *= (((vol >> 4) & 7) + 1) / (8 * 15 * 4);
            r *= ((vol & 7) + 1) / (8 * 15 * 4);
        }
        // remove the constant part (the DACs of the Game Boy are not centred either)
        const h = this.hp;
        const ol = l - h[0] + 0.996 * h[1], or = r - h[2] + 0.996 * h[3];
        h[0] = l; h[1] = ol; h[2] = r; h[3] = or;
        this.buf[this.n++] = ol;
        this.buf[this.n++] = or;
    }

    take() {
        const out = this.buf.slice(0, this.n);
        this.n = 0;
        return out;
    }
}

/* ------------------------------------------------------------------ the machine */

class GameBoy {
    constructor(rom, opts = {}) {
        this.rom = rom;
        this.romBanks = Math.max(2, rom.length >> 14);
        this.sram = new Uint8Array(0x2000 * Math.max(1, [0, 1, 1, 4, 16, 8][rom[0x149]] || 1));
        this.vram = new Uint8Array(0x2000);
        this.wram = new Uint8Array(0x2000);
        this.oam = new Uint8Array(0xa0);
        this.hram = new Uint8Array(0x80);
        this.io = new Uint8Array(0x80);
        this.frame = new Uint8Array(160 * 144);
        this.apu = new APU(opts.sampleRate || 48000);
        this.onSave = null;
        // MBC1
        this.ramOn = false; this.romBank = 1; this.bank2 = 0; this.mbcMode = 0; this.sramDirty = false;
        // CPU
        this.a = 0x01; this.f = 0xb0; this.b = 0x00; this.c = 0x13; this.d = 0x00; this.e = 0xd8;
        this.h = 0x01; this.l = 0x4d; this.sp = 0xfffe; this.pc = 0x100;
        this.ime = false; this.imeNext = false; this.halted = false;
        this.ie = 0;
        // timer, display, joypad
        this.divCounter = 0xabcc; this.timaCounter = 0;
        this.dot = 0; this.ly = 0; this.mode3done = false; this.winLine = 0; this.statLine = false;
        this.buttons = 0;       // pressed, bit per button
        this.frameDone = false;
        const io = this.io;
        io[0x00] = 0xcf; io[0x0f] = 0xe1; io[0x40] = 0x91; io[0x41] = 0x85; io[0x47] = 0xfc;
        io[0x48] = 0xff; io[0x49] = 0xff;
    }

    setButton(name, down) {
        const bit = 1 << BUTTONS[name];
        if (down && !(this.buttons & bit))
            this.io[0x0f] |= 0x10;
        this.buttons = down ? this.buttons | bit : this.buttons & ~bit;
    }

    takeAudio() {
        return this.apu.take();
    }

    /* ---- memory ---- */

    read(a) {
        if (a < 0x4000)
            return this.rom[this.mbcMode && this.romBanks > 32 ? (((this.bank2 << 5) % this.romBanks) << 14) | a : a];
        if (a < 0x8000) {
            const bank = ((this.bank2 << 5) | this.romBank) % this.romBanks;
            return this.rom[(bank << 14) | (a & 0x3fff)];
        }
        if (a < 0xa000)
            return this.vram[a & 0x1fff];
        if (a < 0xc000) {
            if (!this.ramOn)
                return 0xff;
            return this.sram[(this.ramOffset() + (a & 0x1fff)) % this.sram.length];
        }
        if (a < 0xfe00)
            return this.wram[a & 0x1fff];
        if (a < 0xfea0)
            return this.oam[a - 0xfe00];
        if (a < 0xff00)
            return 0xff;
        if (a < 0xff80)
            return this.readIO(a);
        if (a < 0xffff)
            return this.hram[a - 0xff80];
        return this.ie;
    }

    ramOffset() {
        return this.mbcMode ? this.bank2 << 13 : 0;
    }

    write(a, v) {
        v &= 0xff;
        if (a < 0x2000) {
            const on = (v & 0x0f) === 0x0a;
            if (this.ramOn && !on && this.sramDirty) {
                this.sramDirty = false;
                if (this.onSave)
                    this.onSave(this.sram);
            }
            this.ramOn = on;
        } else if (a < 0x4000) {
            this.romBank = (v & 0x1f) || 1;
        } else if (a < 0x6000) {
            this.bank2 = v & 3;
        } else if (a < 0x8000) {
            this.mbcMode = v & 1;
        } else if (a < 0xa000) {
            this.vram[a & 0x1fff] = v;
        } else if (a < 0xc000) {
            if (this.ramOn) {
                this.sram[(this.ramOffset() + (a & 0x1fff)) % this.sram.length] = v;
                this.sramDirty = true;
            }
        } else if (a < 0xfe00) {
            this.wram[a & 0x1fff] = v;
        } else if (a < 0xfea0) {
            this.oam[a - 0xfe00] = v;
        } else if (a < 0xff00) {
            // unusable
        } else if (a < 0xff80) {
            this.writeIO(a, v);
        } else if (a < 0xffff) {
            this.hram[a - 0xff80] = v;
        } else {
            this.ie = v;
        }
    }

    readIO(a) {
        const r = a & 0x7f;
        switch (r) {
        case 0x00: {
            const sel = this.io[0];
            let lo = 0x0f;
            if (!(sel & 0x10)) lo &= ~(this.buttons & 0x0f);
            if (!(sel & 0x20)) lo &= ~(this.buttons >> 4);
            return 0xc0 | (sel & 0x30) | lo;
        }
        case 0x04: return (this.divCounter >> 8) & 0xff;
        case 0x0f: return this.io[0x0f] | 0xe0;
        case 0x41: return 0x80 | (this.io[0x41] & 0x78) | (this.ly === this.io[0x45] ? 4 : 0) | this.mode();
        case 0x44: return this.ly;
        }
        if (r >= 0x10 && r < 0x40)
            return this.apu.read(a);
        return this.io[r];
    }

    writeIO(a, v) {
        const r = a & 0x7f;
        if (r >= 0x10 && r < 0x40) {
            this.apu.write(a, v);
            return;
        }
        switch (r) {
        case 0x00: this.io[0] = v & 0x30; return;
        case 0x04: this.divCounter = 0; return;
        case 0x41: this.io[0x41] = v & 0x78; this.updateStat(); return;
        case 0x44: return;
        case 0x46: {        // OAM DMA (at once)
            const src = v << 8;
            for (let i = 0; i < 0xa0; i++)
                this.oam[i] = this.read(src + i);
            return;
        }
        case 0x40:
            if ((this.io[0x40] & 0x80) && !(v & 0x80)) {      // display off
                this.ly = 0;
                this.dot = 0;
                this.frame.fill(0);
            }
            this.io[0x40] = v;
            return;
        }
        this.io[r] = v;
    }

    /* ---- the display ---- */

    mode() {
        if (!(this.io[0x40] & 0x80))
            return 0;
        if (this.ly >= 144)
            return 1;
        if (this.dot < 80)
            return 2;
        return this.dot < 252 ? 3 : 0;
    }

    /* the STAT interrupt fires when one of its enabled conditions becomes true */
    updateStat() {
        const s = this.io[0x41], m = this.mode();
        const line = (this.io[0x40] & 0x80) && (((s & 0x40) && this.ly === this.io[0x45]) ||
            ((s & 0x20) && m === 2) || ((s & 0x10) && m === 1) || ((s & 0x08) && m === 0));
        if (line && !this.statLine)
            this.io[0x0f] |= 2;
        this.statLine = !!line;
    }

    ppu(c) {
        if (!(this.io[0x40] & 0x80))
            return;
        const before = this.dot;
        this.dot += c;
        if (this.ly < 144 && before < 252 && this.dot >= 252)
            this.renderLine();
        if (this.dot >= 456) {
            this.dot -= 456;
            this.ly++;
            if (this.ly === 144) {
                this.io[0x0f] |= 1;
                this.frameDone = true;
            } else if (this.ly > 153) {
                this.ly = 0;
                this.winLine = 0;
            }
        }
        this.updateStat();
    }

    renderLine() {
        const io = this.io, lcdc = io[0x40], ly = this.ly, vram = this.vram, out = this.frame;
        const row = ly * 160, bgIdx = this.lineBg || (this.lineBg = new Uint8Array(160));
        const bgp = io[0x47];
        const unsigned = lcdc & 0x10;
        const tileAddr = t => unsigned ? t << 4 : 0x1000 + (((t << 24) >> 24) << 4);

        if (lcdc & 1) {
            const map = lcdc & 0x08 ? 0x1c00 : 0x1800, y = (ly + io[0x42]) & 255, scx = io[0x43];
            for (let x = 0; x < 160; x++) {
                const bx = (x + scx) & 255;
                const t = vram[map + ((y >> 3) << 5) + (bx >> 3)];
                const p = tileAddr(t) + ((y & 7) << 1), bit = 7 - (bx & 7);
                const ci = ((vram[p] >> bit) & 1) | (((vram[p + 1] >> bit) & 1) << 1);
                bgIdx[x] = ci;
                out[row + x] = (bgp >> (ci * 2)) & 3;
            }
            const wx = io[0x4b] - 7, wy = io[0x4a];
            if ((lcdc & 0x20) && ly >= wy && wx < 160) {
                const map2 = lcdc & 0x40 ? 0x1c00 : 0x1800, y2 = this.winLine;
                for (let x = Math.max(0, wx); x < 160; x++) {
                    const bx = x - wx;
                    const t = vram[map2 + ((y2 >> 3) << 5) + (bx >> 3)];
                    const p = tileAddr(t) + ((y2 & 7) << 1), bit = 7 - (bx & 7);
                    const ci = ((vram[p] >> bit) & 1) | (((vram[p + 1] >> bit) & 1) << 1);
                    bgIdx[x] = ci;
                    out[row + x] = (bgp >> (ci * 2)) & 3;
                }
                this.winLine++;
            }
        } else {
            bgIdx.fill(0);
            out.fill(0, row, row + 160);
        }

        if (lcdc & 2) {
            const h = lcdc & 4 ? 16 : 8, oam = this.oam, list = [];
            for (let i = 0; i < 40 && list.length < 10; i++) {
                const sy = oam[i * 4] - 16;
                if (ly >= sy && ly < sy + h)
                    list.push(i);
            }
            // the lower x wins, then the lower number: draw the others first
            list.sort((p, q) => (oam[q * 4 + 1] - oam[p * 4 + 1]) || (q - p));
            for (const i of list) {
                const sy = oam[i * 4] - 16, sx = oam[i * 4 + 1] - 8, attr = oam[i * 4 + 3];
                let t = oam[i * 4 + 2], line = ly - sy;
                if (h === 16) t &= 0xfe;
                if (attr & 0x40) line = h - 1 - line;
                const p = (t << 4) + (line << 1), pal = attr & 0x10 ? io[0x49] : io[0x48];
                for (let k = 0; k < 8; k++) {
                    const x = sx + k;
                    if (x < 0 || x >= 160)
                        continue;
                    const bit = attr & 0x20 ? k : 7 - k;
                    const ci = ((vram[p] >> bit) & 1) | (((vram[p + 1] >> bit) & 1) << 1);
                    if (!ci || ((attr & 0x80) && bgIdx[x]))
                        continue;
                    out[row + x] = (pal >> (ci * 2)) & 3;
                }
            }
        }
    }

    /* ---- the timer ---- */

    timer(c) {
        this.divCounter = (this.divCounter + c) & 0xffff;
        const tac = this.io[0x07];
        if (!(tac & 4))
            return;
        const period = [1024, 16, 64, 256][tac & 3];
        this.timaCounter += c;
        while (this.timaCounter >= period) {
            this.timaCounter -= period;
            if (++this.io[0x05] > 0xff || this.io[0x05] === 0) {
                this.io[0x05] = this.io[0x06];
                this.io[0x0f] |= 4;
            }
        }
    }

    /* ---- the CPU ---- */

    get hl() { return (this.h << 8) | this.l; }
    set hl(v) { this.h = (v >> 8) & 0xff; this.l = v & 0xff; }

    fetch() {
        const v = this.read(this.pc);
        this.pc = (this.pc + 1) & 0xffff;
        return v;
    }
    fetch16() {
        const lo = this.fetch();
        return lo | (this.fetch() << 8);
    }
    push(v) {
        this.sp = (this.sp - 1) & 0xffff;
        this.write(this.sp, v >> 8);
        this.sp = (this.sp - 1) & 0xffff;
        this.write(this.sp, v & 0xff);
    }
    pop() {
        const lo = this.read(this.sp);
        const hi = this.read((this.sp + 1) & 0xffff);
        this.sp = (this.sp + 2) & 0xffff;
        return lo | (hi << 8);
    }

    getR(i) {
        switch (i) {
        case 0: return this.b; case 1: return this.c; case 2: return this.d; case 3: return this.e;
        case 4: return this.h; case 5: return this.l; case 6: return this.read(this.hl); default: return this.a;
        }
    }
    setR(i, v) {
        switch (i) {
        case 0: this.b = v; break; case 1: this.c = v; break; case 2: this.d = v; break; case 3: this.e = v; break;
        case 4: this.h = v; break; case 5: this.l = v; break; case 6: this.write(this.hl, v); break;
        default: this.a = v;
        }
    }
    getRR(i) {      // BC DE HL SP
        switch (i) {
        case 0: return (this.b << 8) | this.c; case 1: return (this.d << 8) | this.e;
        case 2: return this.hl; default: return this.sp;
        }
    }
    setRR(i, v) {
        v &= 0xffff;
        switch (i) {
        case 0: this.b = v >> 8; this.c = v & 0xff; break;
        case 1: this.d = v >> 8; this.e = v & 0xff; break;
        case 2: this.hl = v; break;
        default: this.sp = v;
        }
    }
    flags(z, n, h, c) {
        this.f = (z ? 0x80 : 0) | (n ? 0x40 : 0) | (h ? 0x20 : 0) | (c ? 0x10 : 0);
    }
    get cf() { return (this.f >> 4) & 1; }
    get zf() { return (this.f >> 7) & 1; }
    cond(i) {       // NZ Z NC C
        switch (i) {
        case 0: return !this.zf; case 1: return !!this.zf; case 2: return !this.cf; default: return !!this.cf;
        }
    }

    alu(op, v) {
        const a = this.a;
        let r;
        switch (op) {
        case 0: r = a + v; this.flags(!(r & 0xff), 0, (a & 15) + (v & 15) > 15, r > 0xff); break;
        case 1: { const c = this.cf; r = a + v + c; this.flags(!(r & 0xff), 0, (a & 15) + (v & 15) + c > 15, r > 0xff); break; }
        case 2: r = a - v; this.flags(!(r & 0xff), 1, (a & 15) < (v & 15), r < 0); break;
        case 3: { const c = this.cf; r = a - v - c; this.flags(!(r & 0xff), 1, (a & 15) - (v & 15) - c < 0, r < 0); break; }
        case 4: r = a & v; this.flags(!r, 0, 1, 0); break;
        case 5: r = a ^ v; this.flags(!r, 0, 0, 0); break;
        case 6: r = a | v; this.flags(!r, 0, 0, 0); break;
        default: r = a - v; this.flags(!(r & 0xff), 1, (a & 15) < (v & 15), r < 0); return;     // CP
        }
        this.a = r & 0xff;
    }

    cb() {
        const op = this.fetch(), i = op & 7, x = op >> 6, y = (op >> 3) & 7;
        let v = this.getR(i);
        if (x === 1) {          // BIT
            this.f = (this.f & 0x10) | 0x20 | ((v >> y) & 1 ? 0 : 0x80);
            return i === 6 ? 12 : 8;
        }
        if (x === 2)
            v &= ~(1 << y);
        else if (x === 3)
            v |= 1 << y;
        else {
            let c;
            switch (y) {
            case 0: c = v >> 7; v = ((v << 1) | c) & 0xff; break;                  // RLC
            case 1: c = v & 1; v = (v >> 1) | (c << 7); break;                     // RRC
            case 2: c = v >> 7; v = ((v << 1) | this.cf) & 0xff; break;           // RL
            case 3: c = v & 1; v = (v >> 1) | (this.cf << 7); break;              // RR
            case 4: c = v >> 7; v = (v << 1) & 0xff; break;                        // SLA
            case 5: c = v & 1; v = (v >> 1) | (v & 0x80); break;                   // SRA
            case 6: c = 0; v = ((v << 4) | (v >> 4)) & 0xff; break;                // SWAP
            default: c = v & 1; v >>= 1;                                           // SRL
            }
            this.flags(!v, 0, 0, c);
        }
        this.setR(i, v);
        return i === 6 ? 16 : 8;
    }

    /* one instruction (or an interrupt, or a halted step); returns the clock cycles */
    step() {
        const pending = this.ie & this.io[0x0f] & 0x1f;
        if (this.halted) {
            if (!pending)
                return 4;
            this.halted = false;
        }
        if (this.ime && pending) {
            const n = 31 - Math.clz32(pending & -pending);
            this.io[0x0f] &= ~(1 << n);
            this.ime = false;
            this.push(this.pc);
            this.pc = 0x40 + n * 8;
            return 20;
        }
        if (this.imeNext) {
            this.imeNext = false;
            this.ime = true;
        }
        const op = this.fetch();
        let cyc = CYCLES[op] * 4;
        if (op >= 0x40 && op < 0x80) {
            if (op === 0x76)
                this.halted = true;
            else
                this.setR((op >> 3) & 7, this.getR(op & 7));
            return cyc;
        }
        if (op >= 0x80 && op < 0xc0) {
            this.alu((op >> 3) & 7, this.getR(op & 7));
            return cyc;
        }
        let v, t;
        switch (op) {
        case 0x00: break;
        case 0x01: case 0x11: case 0x21: case 0x31: this.setRR(op >> 4, this.fetch16()); break;
        case 0x02: this.write(this.getRR(0), this.a); break;
        case 0x12: this.write(this.getRR(1), this.a); break;
        case 0x22: this.write(this.hl, this.a); this.hl = (this.hl + 1) & 0xffff; break;
        case 0x32: this.write(this.hl, this.a); this.hl = (this.hl - 1) & 0xffff; break;
        case 0x0a: this.a = this.read(this.getRR(0)); break;
        case 0x1a: this.a = this.read(this.getRR(1)); break;
        case 0x2a: this.a = this.read(this.hl); this.hl = (this.hl + 1) & 0xffff; break;
        case 0x3a: this.a = this.read(this.hl); this.hl = (this.hl - 1) & 0xffff; break;
        case 0x03: case 0x13: case 0x23: case 0x33: this.setRR(op >> 4, this.getRR(op >> 4) + 1); break;
        case 0x0b: case 0x1b: case 0x2b: case 0x3b: this.setRR(op >> 4, this.getRR(op >> 4) - 1); break;
        case 0x04: case 0x0c: case 0x14: case 0x1c: case 0x24: case 0x2c: case 0x34: case 0x3c:
            v = (this.getR(op >> 3) + 1) & 0xff;
            this.setR(op >> 3, v);
            this.f = (this.f & 0x10) | (v ? 0 : 0x80) | ((v & 15) === 0 ? 0x20 : 0);
            break;
        case 0x05: case 0x0d: case 0x15: case 0x1d: case 0x25: case 0x2d: case 0x35: case 0x3d:
            v = (this.getR(op >> 3) - 1) & 0xff;
            this.setR(op >> 3, v);
            this.f = (this.f & 0x10) | 0x40 | (v ? 0 : 0x80) | ((v & 15) === 15 ? 0x20 : 0);
            break;
        case 0x06: case 0x0e: case 0x16: case 0x1e: case 0x26: case 0x2e: case 0x36: case 0x3e:
            this.setR(op >> 3, this.fetch());
            break;
        case 0x07: t = this.a >> 7; this.a = ((this.a << 1) | t) & 0xff; this.flags(0, 0, 0, t); break;
        case 0x0f: t = this.a & 1; this.a = (this.a >> 1) | (t << 7); this.flags(0, 0, 0, t); break;
        case 0x17: t = this.a >> 7; this.a = ((this.a << 1) | this.cf) & 0xff; this.flags(0, 0, 0, t); break;
        case 0x1f: t = this.a & 1; this.a = (this.a >> 1) | (this.cf << 7); this.flags(0, 0, 0, t); break;
        case 0x08: v = this.fetch16(); this.write(v, this.sp & 0xff); this.write((v + 1) & 0xffff, this.sp >> 8); break;
        case 0x09: case 0x19: case 0x29: case 0x39: {
            const hl = this.hl, rr = this.getRR(op >> 4), r = hl + rr;
            this.f = (this.f & 0x80) | ((hl & 0xfff) + (rr & 0xfff) > 0xfff ? 0x20 : 0) | (r > 0xffff ? 0x10 : 0);
            this.hl = r & 0xffff;
            break;
        }
        case 0x10: this.fetch(); break;            // STOP
        case 0x18: v = this.fetch(); this.pc = (this.pc + ((v << 24) >> 24)) & 0xffff; break;
        case 0x20: case 0x28: case 0x30: case 0x38:
            v = this.fetch();
            if (this.cond((op >> 3) & 3)) {
                this.pc = (this.pc + ((v << 24) >> 24)) & 0xffff;
                cyc += 4;
            }
            break;
        case 0x27: {        // DAA
            let a = this.a, c = this.cf;
            const n = this.f & 0x40, h = this.f & 0x20;
            if (!n) {
                if (c || a > 0x99) { a += 0x60; c = 1; }
                if (h || (a & 15) > 9) a += 6;
            } else {
                if (c) a -= 0x60;
                if (h) a -= 6;
            }
            this.a = a & 0xff;
            this.f = (this.a ? 0 : 0x80) | n | (c ? 0x10 : 0);
            break;
        }
        case 0x2f: this.a ^= 0xff; this.f |= 0x60; break;
        case 0x37: this.f = (this.f & 0x80) | 0x10; break;
        case 0x3f: this.f = (this.f & 0x90) ^ 0x10; break;
        case 0xc0: case 0xc8: case 0xd0: case 0xd8:
            if (this.cond((op >> 3) & 3)) {
                this.pc = this.pop();
                cyc += 12;
            }
            break;
        case 0xc1: case 0xd1: case 0xe1: this.setRR((op >> 4) & 3, this.pop()); break;
        case 0xf1: v = this.pop(); this.a = v >> 8; this.f = v & 0xf0; break;
        case 0xc5: case 0xd5: case 0xe5: this.push(this.getRR((op >> 4) & 3)); break;
        case 0xf5: this.push((this.a << 8) | this.f); break;
        case 0xc2: case 0xca: case 0xd2: case 0xda:
            v = this.fetch16();
            if (this.cond((op >> 3) & 3)) {
                this.pc = v;
                cyc += 4;
            }
            break;
        case 0xc3: this.pc = this.fetch16(); break;
        case 0xc4: case 0xcc: case 0xd4: case 0xdc:
            v = this.fetch16();
            if (this.cond((op >> 3) & 3)) {
                this.push(this.pc);
                this.pc = v;
                cyc += 12;
            }
            break;
        case 0xcd: v = this.fetch16(); this.push(this.pc); this.pc = v; break;
        case 0xc6: case 0xce: case 0xd6: case 0xde: case 0xe6: case 0xee: case 0xf6: case 0xfe:
            this.alu((op >> 3) & 7, this.fetch());
            break;
        case 0xc7: case 0xcf: case 0xd7: case 0xdf: case 0xe7: case 0xef: case 0xf7: case 0xff:
            this.push(this.pc);
            this.pc = op & 0x38;
            break;
        case 0xc9: this.pc = this.pop(); break;
        case 0xd9: this.pc = this.pop(); this.ime = true; break;
        case 0xcb: cyc = this.cb(); break;
        case 0xe0: this.write(0xff00 | this.fetch(), this.a); break;
        case 0xf0: this.a = this.read(0xff00 | this.fetch()); break;
        case 0xe2: this.write(0xff00 | this.c, this.a); break;
        case 0xf2: this.a = this.read(0xff00 | this.c); break;
        case 0xe8: case 0xf8: {
            const e = this.fetch(), s = this.sp, r = (s + ((e << 24) >> 24)) & 0xffff;
            this.flags(0, 0, (s & 15) + (e & 15) > 15, (s & 0xff) + e > 0xff);
            if (op === 0xe8) this.sp = r; else this.hl = r;
            break;
        }
        case 0xf9: this.sp = this.hl; break;
        case 0xe9: this.pc = this.hl; break;
        case 0xea: this.write(this.fetch16(), this.a); break;
        case 0xfa: this.a = this.read(this.fetch16()); break;
        case 0xf3: this.ime = false; this.imeNext = false; break;
        case 0xfb: this.imeNext = true; break;
        default:        // the missing opcodes lock the CPU up
            this.pc = (this.pc - 1) & 0xffff;
        }
        return cyc;
    }

    /* runs until the next frame is complete (or the time of one, with the display off) */
    runFrame() {
        this.frameDone = false;
        let total = 0;
        while (!this.frameDone && total < FRAME_CYCLES * 2) {
            const c = this.step();
            total += c;
            this.timer(c);
            this.ppu(c);
            this.apu.tick(c);
            if (!(this.io[0x40] & 0x80) && total >= FRAME_CYCLES)
                break;
        }
        return total;
    }
}

if (typeof module !== 'undefined')
    module.exports = { GameBoy };
