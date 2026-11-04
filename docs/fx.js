/*
 * fx.js - small motions on the page: sections slide in when they come into view, the letters
 * of the title drop in like tiles, the screenshots tilt toward the pointer.
 */
(function () {
    'use strict';

    const still = window.matchMedia('(prefers-reduced-motion: reduce)').matches;
    if (still || !('IntersectionObserver' in window))
        return;

    // the title: every letter its own tile
    const h1 = document.querySelector('.hero h1');
    if (h1) {
        const text = h1.textContent;
        h1.setAttribute('aria-label', text);
        h1.textContent = '';
        [...text].forEach((ch, i) => {
            const s = document.createElement('span');
            s.textContent = ch;
            s.setAttribute('aria-hidden', 'true');
            s.style.animationDelay = (0.08 * i) + 's';
            h1.appendChild(s);
        });
        h1.classList.add('drop');
    }

    const io = new IntersectionObserver(entries => entries.forEach(e => {
        if (e.isIntersecting) {
            e.target.classList.add('in');
            io.unobserve(e.target);
        }
    }), { rootMargin: '0px 0px -8% 0px' });
    document.querySelectorAll('.step, .ticket, .shots figure, .panel, .keys li, .kinds li')
        .forEach((el, i) => {
            el.classList.add('reveal');
            el.style.transitionDelay = (i % 6) * 0.06 + 's';
            io.observe(el);
        });

    document.querySelectorAll('.shots figure').forEach(f => {
        f.addEventListener('pointermove', ev => {
            const r = f.getBoundingClientRect();
            const x = (ev.clientX - r.left) / r.width - 0.5, y = (ev.clientY - r.top) / r.height - 0.5;
            f.style.transform = 'perspective(700px) rotateY(' + (x * 10) + 'deg) rotateX(' + (-y * 10) + 'deg) translateY(-4px)';
        });
        f.addEventListener('pointerleave', () => { f.style.transform = ''; });
    });
}());
