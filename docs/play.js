/*
 * play.js - the cabinet on the page: runs tilomat.gb in the emulator of gb.js, paints the
 * picture in the palette of the page, plays the sound, and connects the keyboard and the
 * buttons of the cabinet. The cartridge RAM (the high score list) stays in localStorage.
 */
'use strict';

(() => {
    const SHADES = [[0xf2, 0xed, 0xe1], [0xb9, 0xb2, 0xa2], [0x6b, 0x65, 0x58], [0x23, 0x21, 0x1d]];
    const FRAME_RATE = 4194304 / 70224;
    const STORE = 'tilomat-gb.sram';
    const BUFFER = 0.1;                 // seconds of sound kept queued
    const KEYMAP = {
        ArrowLeft: 'left', ArrowRight: 'right', ArrowUp: 'up', ArrowDown: 'down',
        KeyA: 'left', KeyD: 'right', KeyW: 'up', KeyS: 'down',
        KeyX: 'a', KeyK: 'a', KeyZ: 'b', KeyY: 'b', KeyJ: 'b',
        Enter: 'start', Space: 'start', ShiftLeft: 'select', ShiftRight: 'select', Backspace: 'select',
    };
    const BUTTONS = ['left', 'right', 'up', 'down', 'a', 'b', 'select', 'start'];

    const cabinet = document.getElementById('cabinet');
    const canvas = document.getElementById('screen');
    const powerBtn = document.getElementById('power');
    const muteBtn = document.getElementById('mute');
    const pad = document.getElementById('pad');
    const ctx = canvas.getContext('2d');
    const picture = ctx.createImageData(160, 144);

    let gb = null, audio = null, volume = null;
    let running = false, soundOn = true, queuedUntil = 0, lastTime = 0, owed = 0, frameNo = 0;

    /* The game looks at the buttons once per frame: a press is held for two frames at least, so
     * that a quick tap is never lost. */
    const downSince = {}, letGo = new Set();

    function setButton(name, down) {
        if (!gb)
            return;
        if (down) {
            letGo.delete(name);
            downSince[name] = frameNo;
            gb.setButton(name, true);
        } else if (frameNo - (downSince[name] ?? -10) < 2) {
            letGo.add(name);
        } else {
            gb.setButton(name, false);
        }
    }

    function stepFrame() {
        gb.runFrame();
        frameNo++;
        for (const name of letGo)
            if (frameNo - downSince[name] >= 2) {
                gb.setButton(name, false);
                letGo.delete(name);
            }
    }

    function paint() {
        const src = gb.frame, dst = picture.data;
        for (let i = 0, j = 0; i < src.length; i++, j += 4) {
            const c = SHADES[src[i]];
            dst[j] = c[0];
            dst[j + 1] = c[1];
            dst[j + 2] = c[2];
            dst[j + 3] = 255;
        }
        ctx.putImageData(picture, 0, 0);
    }

    function queueSound() {
        const s = gb.takeAudio();
        if (!s.length)
            return;
        const n = s.length >> 1;
        const buf = audio.createBuffer(2, n, audio.sampleRate);
        const left = buf.getChannelData(0), right = buf.getChannelData(1);
        for (let i = 0; i < n; i++) {
            left[i] = s[2 * i];
            right[i] = s[2 * i + 1];
        }
        const node = audio.createBufferSource();
        node.buffer = buf;
        node.connect(volume);
        node.start(queuedUntil);
        queuedUntil += n / audio.sampleRate;
    }

    /* With sound, the sound card keeps the time: frames run until a tenth of a second of sound
     * is queued, so the music neither stutters nor drifts. Without, the animation clock does. */
    function tick(now) {
        if (!running)
            return;
        let ran = 0;
        if (audio && audio.state === 'running') {
            const t = audio.currentTime;
            if (queuedUntil < t + 0.01)
                queuedUntil = t + 0.05;
            while (queuedUntil < t + BUFFER && ran < 8) {
                stepFrame();
                queueSound();
                ran++;
            }
            lastTime = 0;
        } else {
            owed += (lastTime ? Math.min(now - lastTime, 100) : 1000 / 60) * FRAME_RATE / 1000;
            lastTime = now;
            while (owed >= 1 && ran < 4) {
                stepFrame();
                gb.takeAudio();
                owed--;
                ran++;
            }
            if (owed > 4)
                owed = 0;
        }
        if (ran)
            paint();
        requestAnimationFrame(tick);
    }

    function loadRam() {
        try {
            const s = localStorage.getItem(STORE);
            if (s)
                gb.sram.set(Uint8Array.from(atob(s), c => c.charCodeAt(0)).subarray(0, gb.sram.length));
        } catch (e) {
            /* no storage: the list lasts as long as the page */
        }
    }

    function storeRam(sram) {
        try {
            localStorage.setItem(STORE, btoa(String.fromCharCode(...sram)));
        } catch (e) {
            /* see above */
        }
    }

    function run() {
        running = true;
        lastTime = 0;
        owed = 0;
        cabinet.classList.add('on');
        requestAnimationFrame(tick);
    }

    function halt() {
        running = false;
        cabinet.classList.remove('on');
    }

    async function switchOn() {
        if (gb) {
            if (!running)
                run();
            return;
        }
        powerBtn.disabled = true;
        try {
            audio = new AudioContext();
            volume = audio.createGain();
            volume.gain.value = soundOn ? 1 : 0;
            volume.connect(audio.destination);
        } catch (e) {
            audio = null;
        }
        try {
            const res = await fetch('tilomat.gb');
            if (!res.ok)
                throw new Error(String(res.status));
            const rom = new Uint8Array(await res.arrayBuffer());
            gb = new GameBoy(rom, { sampleRate: audio ? audio.sampleRate : 48000 });
        } catch (e) {
            cabinet.classList.add('failed');
            powerBtn.disabled = false;
            return;
        }
        loadRam();
        gb.onSave = storeRam;
        cabinet.classList.add('booted');
        run();
        cabinet.focus({ preventScroll: true });
    }

    powerBtn.addEventListener('click', switchOn);

    muteBtn.addEventListener('click', () => {
        soundOn = !soundOn;
        if (volume)
            volume.gain.value = soundOn ? 1 : 0;
        muteBtn.setAttribute('aria-pressed', String(!soundOn));
        muteBtn.textContent = soundOn ? 'SOUND' : 'MUTED';
    });

    /* The keyboard goes to the game while the cabinet has the focus, or while the game runs, no
     * other element has the focus and the cabinet is on screen. */
    function keysForGame() {
        const el = document.activeElement;
        if (el === cabinet)
            return true;
        if (!running || (el && el !== document.body))
            return false;
        const r = cabinet.getBoundingClientRect();
        return r.bottom > 0 && r.top < innerHeight;
    }

    function onKey(ev, down) {
        const name = KEYMAP[ev.code];
        if (!name || !gb || ev.altKey || ev.ctrlKey || ev.metaKey || (down && !keysForGame()))
            return;
        ev.preventDefault();
        if (ev.repeat)
            return;
        setButton(name, down);
        const el = cabinet.querySelector(`[data-btn="${name}"]`);
        if (el)
            el.classList.toggle('down', down);
        else
            pad.dataset.dir = down ? name : '';
    }

    document.addEventListener('keydown', ev => onKey(ev, true));
    document.addEventListener('keyup', ev => onKey(ev, false));
    cabinet.addEventListener('blur', () => {
        if (gb)
            for (const name of BUTTONS)
                setButton(name, false);
        cabinet.querySelectorAll('.down').forEach(el => el.classList.remove('down'));
        pad.dataset.dir = '';
    });

    /* the buttons of the cabinet, by mouse and touch */
    for (const el of cabinet.querySelectorAll('[data-btn]')) {
        const name = el.dataset.btn;
        const release = () => {
            el.classList.remove('down');
            setButton(name, false);
        };
        el.addEventListener('pointerdown', ev => {
            ev.preventDefault();
            if (!gb) {
                switchOn();
                return;
            }
            el.setPointerCapture(ev.pointerId);
            el.classList.add('down');
            setButton(name, true);
            cabinet.focus({ preventScroll: true });
        });
        el.addEventListener('pointerup', release);
        el.addEventListener('pointercancel', release);
    }

    /* the cross: the direction from its middle, following a sliding finger */
    let padDir = null;

    function padTo(ev) {
        const r = pad.getBoundingClientRect();
        const x = ev.clientX - r.left - r.width / 2, y = ev.clientY - r.top - r.height / 2;
        let dir = null;
        if (Math.hypot(x, y) >= r.width * 0.1)
            dir = Math.abs(x) > Math.abs(y) ? (x > 0 ? 'right' : 'left') : (y > 0 ? 'down' : 'up');
        if (dir === padDir)
            return;
        if (padDir)
            setButton(padDir, false);
        if (dir)
            setButton(dir, true);
        padDir = dir;
        pad.dataset.dir = dir || '';
    }

    function padRelease() {
        if (padDir)
            setButton(padDir, false);
        padDir = null;
        pad.dataset.dir = '';
    }

    pad.addEventListener('pointerdown', ev => {
        ev.preventDefault();
        if (!gb) {
            switchOn();
            return;
        }
        pad.setPointerCapture(ev.pointerId);
        cabinet.focus({ preventScroll: true });
        padTo(ev);
    });
    pad.addEventListener('pointermove', ev => {
        if (pad.hasPointerCapture(ev.pointerId))
            padTo(ev);
    });
    pad.addEventListener('pointerup', padRelease);
    pad.addEventListener('pointercancel', padRelease);

    /* the game pauses while the page is hidden */
    let resume = false;
    document.addEventListener('visibilitychange', () => {
        if (!gb)
            return;
        if (document.hidden) {
            resume = running;
            halt();
        } else if (resume) {
            run();
        }
    });
})();
