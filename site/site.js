/* RackAttack Editor's site: the pinball screen, the install tutorials, a toy sequencer that plays, the ghosts at work. */
(() => {
'use strict';
const $ = (s, r = document) => r.querySelector(s);
const $$ = (s, r = document) => [...r.querySelectorAll(s)];
const still = matchMedia('(prefers-reduced-motion: reduce)').matches;
const REPO = 'distortedimages-code/rack-attack';
const sleep = ms => new Promise(r => setTimeout(r, ms));

/* runs fn while el is on screen ------------------------------------------------------------------------------------- */
function whenVisible (el, on, off) {
  new IntersectionObserver(es => es.forEach(e => (e.isIntersecting ? on : off || (() => {}))()), { threshold: 0.15 }).observe(el);
}

/* the version and the download: the latest release, the disk image when it has one -------------------------------- */
fetch('https://api.github.com/repos/' + REPO + '/releases/latest')
  .then(r => (r.ok ? r.json() : null))
  .then(rel => {
    if (!rel || !rel.tag_name) return;
    $$('.ver').forEach(e => { e.textContent = rel.tag_name; });
    const assets = rel.assets || [];
    const pick = assets.find(a => /\.dmg$/i.test(a.name)) || assets.find(a => /mac\.zip$/i.test(a.name));
    if (pick) { $('#dl').href = pick.browser_download_url; $('#dl-file').textContent = pick.name; }
  })
  .catch(() => {});

/* the top bar follows the page --------------------------------------------------------------------------------------- */
const navLinks = $$('.bar nav a');
const spy = new IntersectionObserver(es => es.forEach(e => {
  if (!e.isIntersecting) return;
  navLinks.forEach(a => a.classList.toggle('on', a.getAttribute('href') === '#' + e.target.id));
}), { rootMargin: '-45% 0px -50% 0px' });
$$('main > section[id]').forEach(s => spy.observe(s));

/* module titles rush in, like the plugin's tabs --------------------------------------------------------------------- */
const rush = new IntersectionObserver(es => es.forEach(e => {
  if (e.isIntersecting) { e.target.classList.add('rush'); rush.unobserve(e.target); }
}), { threshold: 0.3 });
$$('.mod').forEach(m => rush.observe(m));

/* copy buttons --------------------------------------------------------------------------------------------------------- */
$$('.copy').forEach(b => b.addEventListener('click', () => {
  const text = $('#' + b.dataset.copy).textContent;
  const done = () => { b.textContent = 'Copié !'; b.classList.add('ok'); setTimeout(() => { b.textContent = 'Copier'; b.classList.remove('ok'); }, 1600); };
  if (navigator.clipboard) navigator.clipboard.writeText(text).then(done, () => selectIn(b));
  else selectIn(b);
}));
function selectIn (b) { const r = document.createRange(); r.selectNodeContents($('#' + b.dataset.copy)); getSelection().removeAllRanges(); getSelection().addRange(r); }

/* sprites (the same as in the measuring assistant) ------------------------------------------------------------------- */
const INK = { '#': '#fff', '+': '#9a9a9a', '-': '#6f6f6f', 'k': '#000' };
function sprite (g, rows, x, y, p, ink) {
  for (let r = 0; r < rows.length; ++r)
    for (let c = 0; c < rows[r].length; ++c) {
      const col = (ink || INK)[rows[r][c]];
      if (!col) continue;
      g.fillStyle = col; g.fillRect(Math.round(x + c * p), Math.round(y + r * p), p, p);
    }
}
const BODY = ['.....######.....', '...##########...', '..############..', '.##############.', '.##############.', '.##############.',
  '################', '################', '################', '################', '################', '###############+',
  '##############++', '#############+++'];
const SKIRT = [['##.####..####.##', '#...##....##...#'], ['#.####..####.###', '...##....##...##']];
function ghost (g, x, y, p, t, o = {}) {
  sprite(g, BODY, x, y, p);
  sprite(g, SKIRT[Math.floor(t * 4) % 2], x, y + 14 * p, p);
  g.fillStyle = '#000';
  const gx = o.gx || 0, gy = o.gy || 0, blink = (t % 3.3) < 0.12;
  for (const e of [4, 10]) {
    if (o.happy) { g.fillRect(x + (e - 1) * p, y + 6 * p, p, p); g.fillRect(x + e * p, y + 5 * p, 2 * p, p); g.fillRect(x + (e + 2) * p, y + 6 * p, p, p); }
    else if (blink) g.fillRect(x + (e + gx) * p, y + (6 + gy) * p, 2 * p, p);
    else g.fillRect(x + (e + gx) * p, y + (4 + gy) * p, 2 * p, 3 * p);
  }
  if (o.happy) { g.fillRect(x + 6 * p, y + 9 * p, p, p); g.fillRect(x + 9 * p, y + 9 * p, p, p); g.fillRect(x + 7 * p, y + 10 * p, 2 * p, p); }
  else if (o.looking) g.fillRect(x + (6 + gx) * p, y + 10 * p, 4 * p, p);
  else g.fillRect(x + (7 + gx) * p, y + 9 * p, 2 * p, 2 * p);
}

/* the pinball screen ------------------------------------------------------------------------------------------------------- */
(function dmd () {
  const cv = $('#dmd'); if (!cv) return;
  const W = 160, H = 32, S = cv.width / W, g = cv.getContext('2d');
  const off = document.createElement('canvas'); off.width = W; off.height = H;
  const og = off.getContext('2d', { willReadFrequently: true });
  const words = ['RACK·ATTACK', 'EDITOR', '16 PAS', '24 SONS', 'VERROUS', 'SIMULATEUR', 'MIDRONOME ?!', 'GRATUIT', '0 % VERT'];
  const effects = ['rush', 'drop', 'scan', 'rush', 'zoom'];
  const levels = [];
  for (let i = 0; i <= 16; ++i) {     // the dots, from off to lit
    const k = i / 16, d = document.createElement('canvas'); d.width = d.height = Math.ceil(S);
    const dg = d.getContext('2d');
    const r = Math.round(26 + (255 - 26) * k), gg = Math.round(17 + (176 - 17) * k), b = Math.round(4 + (0 - 4) * k);
    dg.fillStyle = `rgb(${r},${gg},${b})`; dg.beginPath(); dg.arc(S / 2, S / 2, S * 0.4, 0, Math.PI * 2); dg.fill();
    if (k > 0.8) { dg.fillStyle = 'rgba(255,240,200,.55)'; dg.fillRect(S * 0.32, S * 0.28, S * 0.18, S * 0.14); }
    levels.push(d);
  }
  const buf = new Float32Array(W * H);
  let idx = -1, mask = null, mw = 0, effect = 'rush', t0 = 0, special = null;

  function maskOf (text) {
    og.clearRect(0, 0, W, H);
    let size = 24;
    for (const s of [24, 16, 8]) { og.font = `700 ${s}px Silk`; if (og.measureText(text).width <= W - 6) { size = s; break; } }
    og.font = `700 ${size}px Silk`; og.textBaseline = 'middle'; og.textAlign = 'center'; og.fillStyle = '#fff';
    og.fillText(text, W / 2, H / 2 + 1);
    const d = og.getImageData(0, 0, W, H).data, m = new Uint8Array(W * H);
    let minX = W, maxX = 0;
    for (let i = 0; i < W * H; ++i) if (d[i * 4 + 3] > 110) { m[i] = 1; const x = i % W; minX = Math.min(minX, x); maxX = Math.max(maxX, x); }
    mw = Math.max(1, maxX - minX + 1);
    return m;
  }
  function next () {
    if (special) { mask = maskOf(special); special = null; effect = 'tilt'; }
    else { idx = (idx + 1) % words.length; mask = maskOf(words[idx]); effect = effects[idx % effects.length]; }
    t0 = performance.now();
  }
  const at = (x, y) => (x >= 0 && x < W && y >= 0 && y < H ? mask[y * W + x] : 0);
  const HOLD = 1.9, LEN = 2.6;

  function frame (now) {
    let t = (now - t0) / 1000;
    if (still) t = 1.0;
    if (t > LEN) { next(); t = 0; }
    buf.fill(0);
    const ease = x => 1 - Math.pow(1 - Math.min(1, Math.max(0, x)), 3);
    for (let y = 0; y < H; ++y)
      for (let x = 0; x < W; ++x) {
        let v = 0;
        if (effect === 'rush') {
          const dx = Math.round((1 - ease(t / 0.28)) * (W + mw));
          v = at(x - dx, y);
          if (!v && t < 0.3) { for (let k = 1; k < 26; ++k) if (at(x - dx + k, y)) { v = Math.max(0, 0.75 - k / 30) * ((y % 2) ? 1 : 0.5); break; } }
        } else if (effect === 'drop') {
          const fall = t < 0.35 ? (1 - ease(t / 0.35)) * -H : Math.round(Math.sin(Math.min(1, (t - 0.35) / 0.25) * Math.PI) * -3);
          v = at(x, y - Math.round(fall));
        } else if (effect === 'scan') {
          const edge = (t / 0.45) * (W + 8);
          v = x < edge - 2 ? at(x, y) : 0;
          if (Math.abs(x - edge) < 2 && t < 0.5) v = Math.max(v, 0.9);
        } else if (effect === 'zoom') {
          const s = t < 0.3 ? 4 - 3 * ease(t / 0.3) : 1;
          v = at(Math.round(W / 2 + (x - W / 2) / s), Math.round(H / 2 + (y - H / 2) / s));
        } else if (effect === 'tilt') {
          v = at(x + (t < 1.2 ? Math.round(Math.sin(t * 60) * 3) : 0), y);
        }
        if (t > 0.3 && t < 0.38 && effect !== 'scan') v = v ? 0 : 0.85;          // the flash
        if (t > HOLD) { const k = (t - HOLD) / (LEN - HOLD); if (((x * 7 + y * 13 + x * y) % 17) / 17 < k * 1.1) v = 0; }   // dissolve
        if (v === 1 && t > 0.4 && t < HOLD) v = 0.86 + 0.14 * Math.sin(now / 90 + x * 0.3);   // shimmer
        buf[y * W + x] = v;
      }
    for (let y = 0; y < H; ++y)
      for (let x = 0; x < W; ++x) g.drawImage(levels[Math.round(buf[y * W + x] * 16)], x * S, y * S);
    if (!still) raf = requestAnimationFrame(frame);
  }
  let raf = 0;
  cv.addEventListener('click', () => { special = 'TILT !'; t0 = -1e9; });
  document.fonts.load('700 16px Silk').then(() => {
    next();
    whenVisible(cv, () => { cancelAnimationFrame(raf); raf = requestAnimationFrame(frame); }, () => cancelAnimationFrame(raf));
  });
})();

/* the ghost peeking over the screenshot: it looks at the mouse ------------------------------------------------------- */
(function peek () {
  const cv = $('#peek'); if (!cv) return;
  const g = cv.getContext('2d'), p = 5;
  let mx = 0, my = 0, raf = 0;
  addEventListener('pointermove', e => { mx = e.clientX; my = e.clientY; }, { passive: true });
  function frame (now) {
    const t = now / 1000, r = cv.getBoundingClientRect();
    const dx = mx - (r.left + r.width / 2), dy = my - (r.top + 40);
    g.clearRect(0, 0, cv.width, cv.height);
    ghost(g, 0, 8 + Math.round(Math.sin(t * 2.4) * 4), p, t, { gx: dx < -60 ? -1 : dx > 60 ? 1 : 0, gy: dy > 80 ? 1 : 0 });
    if (!still) raf = requestAnimationFrame(frame);
  }
  whenVisible(cv, () => { cancelAnimationFrame(raf); raf = requestAnimationFrame(frame); }, () => cancelAnimationFrame(raf));
})();

/* scaled scenes ------------------------------------------------------------------------------------------------------------- */
function fit (stage, w, h) {
  const sc = stage.firstElementChild;
  const go = () => { const k = stage.clientWidth / w; sc.style.transform = `scale(${k})`; stage.style.height = Math.round(h * k) + 'px'; };
  new ResizeObserver(go).observe(stage); go();
}

/* the install tutorials ------------------------------------------------------------------------------------------------------- */
(function install () {
  const stage = $('#scene'); if (!stage) return;
  fit(stage, 640, 300);
  const cur = $('#cur'), term = $('#term'), tt = $('#term-t'), spot = $('#spot'), spotT = $('#spot-t'), alert = $('#alert'), stamp = $('#stamp');
  let route = 'pkg', gen = 0, visible = false, running = false;
  class Stop extends Error {}
  const pos = { x: 560, y: 270 };
  const check = my => { if (my !== gen) throw new Stop(); };
  async function wait (ms, my) { await sleep(ms); check(my); }
  function place (x, y) { pos.x = x; pos.y = y; cur.style.transform = `translate(${x}px, ${y}px)`; }
  async function move (x, y, ms, my, drag) {
    const x0 = pos.x, y0 = pos.y, t0 = performance.now();
    const d0 = drag && { x: parseFloat(drag.dataset.x || 0), y: parseFloat(drag.dataset.y || 0) };
    while (true) {
      const k = Math.min(1, (performance.now() - t0) / ms), e = k < 0.5 ? 2 * k * k : 1 - Math.pow(-2 * k + 2, 2) / 2;
      place(x0 + (x - x0) * e, y0 + (y - y0) * e);
      if (drag) drag.style.transform = `translate(${d0.x + (x - x0) * e}px, ${d0.y + (y - y0) * e}px)`;
      if (k >= 1) break;
      await sleep(16); check(my);
    }
  }
  async function click (my) { cur.classList.remove('click'); void cur.offsetWidth; cur.classList.add('click'); await wait(160, my); }
  function centre (el, dy = 0) {   // an icon's centre in scene coordinates
    let x = el.offsetWidth / 2, y = 22 + dy, n = el;
    while (n && n !== stage.firstElementChild) { x += n.offsetLeft; y += n.offsetTop; n = n.offsetParent; }
    return { x, y };
  }
  async function type (el, text, perChar, my) {     // by the clock, so that slow timers don't slow the typing
    const t0 = performance.now(); let n = 0;
    while (n < text.length) {
      const want = Math.min(text.length, Math.floor((performance.now() - t0) / perChar) + 1);
      el.insertAdjacentText('beforeend', text.slice(n, want)); n = want;
      await wait(16, my);
    }
  }
  function line (html) { tt.querySelector('.c')?.remove(); tt.insertAdjacentHTML('beforeend', html); }
  function reset () {
    [term, spot, alert, stamp].forEach(e => { e.style.display = 'none'; });
    $$('.ic', stage).forEach(i => { i.classList.remove('sel', 'flash', 'hit'); i.style.transform = ''; i.dataset.x = i.dataset.y = 0; i.style.zIndex = ''; i.style.opacity = ''; });
    $$('.spot > span').forEach(k => k.classList.remove('down'));
    $('#alert-ok').classList.remove('down');
    tt.innerHTML = ''; spotT.textContent = '';
    place(560, 270);
  }
  async function terminal (cmd, out, my) {
    spot.style.display = 'block';
    const keys = $$('.spot > span');
    keys[0].classList.add('down'); await wait(220, my); keys[1].classList.add('down'); await wait(300, my);
    keys.forEach(k => k.classList.remove('down'));
    await type(spotT, 'Terminal', 70, my); await wait(450, my);
    spot.style.display = 'none';
    term.style.display = 'block';
    line('<span class="d">~ %</span> <span class="c"></span>'); await wait(300, my);
    tt.querySelector('.c')?.remove();
    await type(tt, cmd, 14, my);
    line('\n<span class="d">Password:</span> <span class="c"></span>'); await wait(1100, my);
    for (const o of out) { line('\n' + o); await wait(380, my); }
    line('\n<span class="d">~ %</span> <span class="c"></span>');
  }
  async function done (text, my) {
    stamp.textContent = text; stamp.style.display = 'block'; stamp.animate([{ transform: 'rotate(-8deg) scale(2.4)', opacity: 0 }, { transform: 'rotate(-8deg) scale(1)', opacity: 1 }], { duration: 160, easing: 'steps(4)' });
    await wait(2600, my);
  }
  const scripts = {
    async pkg (my) {
      const pkg = $('#i-pkg'), c = centre(pkg);
      await wait(500, my);
      await move(c.x, c.y, 900, my);
      pkg.classList.add('sel');
      for (let i = 0; i < 2; ++i) { await click(my); pkg.classList.add('flash'); await wait(90, my); pkg.classList.remove('flash'); }
      await wait(250, my);
      alert.style.display = 'block'; alert.animate([{ transform: 'scale(.6)' }, { transform: 'scale(1)' }], { duration: 120, easing: 'steps(3)' });
      await wait(1700, my);
      const ok = centre($('#alert-ok'), -14);
      await move(ok.x, ok.y, 600, my); await click(my); $('#alert-ok').classList.add('down'); await wait(200, my);
      alert.style.display = 'none'; pkg.classList.remove('sel');
      await move(600, 280, 400, my);
      await terminal('sudo installer -pkg "/Volumes/RackAttack Editor/Installer RackAttack Editor.pkg" -target /',
        ['installer: Package name is RackAttack Editor', 'installer: The install was successful.', '<span class="y">RackAttack Editor est installé.</span>'], my);
      await done('INSTALLÉ !', my);
    },
    async hand (my) {
      await wait(400, my);
      for (const [a, f] of [['#i-au', '#f-au'], ['#i-vst', '#f-vst'], ['#i-app', '#f-app']]) {
        const item = $(a), fold = $(f), ci = centre(item), cf = centre(fold);
        await move(ci.x, ci.y, 600, my);
        item.classList.add('sel'); item.style.zIndex = 5; await wait(160, my);
        await move(cf.x, cf.y, 700, my, item);
        fold.classList.add('hit'); item.style.opacity = 0; item.classList.remove('sel');
        await wait(260, my);
        fold.classList.remove('hit'); item.style.transform = ''; item.style.zIndex = '';
        item.animate([{ opacity: 0 }, { opacity: 1 }], { duration: 200, easing: 'steps(3)', fill: 'forwards' }); item.style.opacity = '';
      }
      await move(600, 280, 500, my);
      await terminal('rm -rf ~/Library/Audio/Plug-Ins/…; sudo sh -c \'… xattr -cr … codesign …\'; auval -v aumu Ratk Dimg …',
        ['<span class="y">RackAttack Editor est prêt.</span>'], my);
      await done('PRÊT !', my);
    },
  };
  async function loop () {
    if (running) return;
    running = true;
    const my = gen;
    try { while (visible && my === gen) { reset(); await scripts[route](my); await wait(400, my); } }
    catch (e) { if (!(e instanceof Stop)) throw e; }
    finally { running = false; if (visible && my !== gen) loop(); }
  }
  function restart () { ++gen; if (!running && visible) loop(); }
  if (still) { reset(); return void $$('.tabs [data-route]').forEach(b => b.addEventListener('click', () => choose(b))); }
  whenVisible(stage, () => { visible = true; restart(); }, () => { visible = false; ++gen; });
  $$('.tabs [data-route]').forEach(b => b.addEventListener('click', () => { choose(b); restart(); }));
  function choose (b) {
    route = b.dataset.route;
    $$('.tabs [data-route]').forEach(x => x.setAttribute('aria-selected', x === b));
    $$('.route').forEach(r => { r.hidden = r.dataset.route !== route; });
  }
})();

/* the toy sequencer: four sounds, sixteen steps, locks -------------------------------------------------------------------- */
(function toy () {
  const grid = $('#grid'); if (!grid) return;
  const names = ['KICK', 'SNARE', 'HAT', 'CLAP'];
  const tracks = names.map(() => ({ steps: Array(16).fill(0), pitch: 0, decay: 64 }));    // 0 off, 1 on, 2 locked
  const start = [[0, 3, 8, 10], [4, 12], [0, 2, 4, 6, 8, 10, 12, 14], [12]];
  start.forEach((s, i) => s.forEach(k => { tracks[i].steps[k] = 1; }));
  tracks[2].steps[14] = 2; tracks[3].steps[15] = 2; tracks[0].steps[10] = 2;
  let sel = 0, undo = null, step = -1;
  const cells = [], nameBtns = [];
  names.forEach((n, r) => {
    const b = document.createElement('button'); b.className = 'nm'; b.textContent = n; b.type = 'button';
    b.addEventListener('click', () => select(r)); grid.appendChild(b); nameBtns.push(b);
    cells.push([]);
    for (let s = 0; s < 16; ++s) { const c = document.createElement('div'); c.className = 'st'; c.textContent = s + 1; c.dataset.r = r; c.dataset.s = s; grid.appendChild(c); cells[r].push(c); }
  });
  function paint () {
    tracks.forEach((t, r) => t.steps.forEach((v, s) => {
      const c = cells[r][s]; c.classList.toggle('on', v === 1); c.classList.toggle('lk', v === 2); c.classList.toggle('now', s === step);
    }));
    nameBtns.forEach((b, r) => b.classList.toggle('sel', r === sel));
  }
  let painting = null;
  function cellAt (e) { const el = document.elementFromPoint(e.clientX, e.clientY); return el && el.classList.contains('st') ? el : null; }
  grid.addEventListener('pointerdown', e => {
    const c = e.target.closest('.st'); if (!c) return;
    e.preventDefault();
    const r = +c.dataset.r, s = +c.dataset.s, t = tracks[r];
    keep();
    if (r !== sel) select(r);
    if (e.shiftKey) { t.steps[s] = t.steps[s] === 2 ? 1 : 2; painting = null; }
    else { painting = { r, v: t.steps[s] ? 0 : 1 }; t.steps[s] = painting.v; }
    paint(); ensureAudio();
    if (t.steps[s] && !playing) trigger(r, ctx ? ctx.currentTime : 0, t.steps[s] === 2);
  });
  grid.addEventListener('pointermove', e => {
    if (!painting || !(e.buttons & 1)) return;
    const c = cellAt(e); if (!c || +c.dataset.r !== painting.r) return;
    const s = +c.dataset.s;
    if (tracks[painting.r].steps[s] !== painting.v) { tracks[painting.r].steps[s] = painting.v; paint(); }
  });
  addEventListener('pointerup', () => { painting = null; });

  /* the two knobs of the chosen sound (pitch goes both ways: zero in the middle) */
  const knobs = {
    pitch: { el: $('.knob[data-k="pitch"]'), min: -12, max: 12, def: 0, centred: true, fmt: v => (v > 0 ? '+' : v < 0 ? '−' : '') + Math.abs(v) },
    decay: { el: $('.knob[data-k="decay"]'), min: 0, max: 127, def: 64, fmt: v => '' + v },
  };
  const arc = (a0, a1) => {
    const r = 17, p = a => [22 + r * Math.sin(a), 22 - r * Math.cos(a)];
    const [x0, y0] = p(a0), [x1, y1] = p(a1);
    return `M${x0.toFixed(2)} ${y0.toFixed(2)} A${r} ${r} 0 ${Math.abs(a1 - a0) > Math.PI ? 1 : 0} ${a1 > a0 ? 1 : 0} ${x1.toFixed(2)} ${y1.toFixed(2)}`;
  };
  function drawKnob (k) {
    const K = knobs[k], v = tracks[sel][k], f = (v - K.min) / (K.max - K.min), lim = Math.PI * 0.75;
    const a = -lim + f * 2 * lim, from = K.centred ? 0 : -lim;
    K.el.querySelector('.ring').setAttribute('d', Math.abs(a - from) < 0.01 ? '' : arc(Math.min(from, a), Math.max(from, a)));
    const dot = K.el.querySelector('.dot');
    dot.setAttribute('x', (22 + 10 * Math.sin(a) - 2).toFixed(1)); dot.setAttribute('y', (22 - 10 * Math.cos(a) - 2).toFixed(1));
    K.el.querySelector('b').textContent = K.fmt(v);
  }
  function setKnob (k, v) {
    const K = knobs[k]; tracks[sel][k] = Math.max(K.min, Math.min(K.max, Math.round(v))); drawKnob(k);
  }
  Object.keys(knobs).forEach(k => {
    const el = knobs[k].el; let y0 = 0, v0 = 0, acc = 0;
    el.addEventListener('pointerdown', e => { el.setPointerCapture(e.pointerId); y0 = e.clientY; v0 = tracks[sel][k]; acc = 0; });
    el.addEventListener('pointermove', e => {
      if (!el.hasPointerCapture(e.pointerId)) return;
      const K = knobs[k], range = K.max - K.min;
      setKnob(k, v0 + (y0 - e.clientY) * range / (e.shiftKey ? 600 : 160));
    });
    el.addEventListener('dblclick', () => setKnob(k, knobs[k].def));
    el.addEventListener('wheel', e => { e.preventDefault(); acc += e.deltaY; if (Math.abs(acc) >= 30) { setKnob(k, tracks[sel][k] - Math.sign(acc)); acc = 0; } }, { passive: false });
    el.addEventListener('keydown', e => {
      const d = { ArrowUp: 1, ArrowRight: 1, ArrowDown: -1, ArrowLeft: -1 }[e.key];
      if (d) { e.preventDefault(); setKnob(k, tracks[sel][k] + d); }
    });
  });
  function select (r) { sel = r; paint(); drawKnob('pitch'); drawKnob('decay'); }

  /* OPTIONS, with Annuler */
  const menu = $('#menu'), optbtn = $('#optbtn');
  optbtn.addEventListener('click', e => { e.stopPropagation(); menu.hidden = !menu.hidden; });
  addEventListener('click', () => { menu.hidden = true; });
  function keep () { undo = JSON.stringify(tracks.map(t => t.steps)); }
  menu.addEventListener('click', e => {
    const op = e.target.dataset.op; if (!op) return;
    const t = tracks[sel].steps;
    if (op === 'undo') { if (undo) { const prev = JSON.parse(undo); keep(); prev.forEach((s, i) => { tracks[i].steps = s; }); } }
    else {
      keep();
      if (op === 'fill4') for (let i = 0; i < 16; i += 4) t[i] = t[i] || 1;
      if (op === 'shift') t.unshift(t.pop());
      if (op === 'random') for (let i = 0; i < 16; ++i) t[i] = Math.random() < 0.3 ? (Math.random() < 0.15 ? 2 : 1) : 0;
      if (op === 'clear') t.fill(0);
      if (op === 'clearall') tracks.forEach(x => x.steps.fill(0));
    }
    paint();
  });

  /* the sound: a tiny drum machine in the browser */
  let ctx = null, out = null, noise = null, playing = false, nextT = 0, timer = 0;
  function ensureAudio () {
    if (ctx) { if (ctx.state === 'suspended') ctx.resume(); return; }
    const AC = window.AudioContext || window.webkitAudioContext; if (!AC) return;
    ctx = new AC();
    const comp = ctx.createDynamicsCompressor(); comp.threshold.value = -14; comp.ratio.value = 4;
    out = ctx.createGain(); out.gain.value = 0.55; out.connect(comp); comp.connect(ctx.destination);
    noise = ctx.createBuffer(1, ctx.sampleRate, ctx.sampleRate);
    const d = noise.getChannelData(0); for (let i = 0; i < d.length; ++i) d[i] = Math.random() * 2 - 1;
  }
  function env (g, t, peak, len) { g.gain.setValueAtTime(0.0001, t); g.gain.exponentialRampToValueAtTime(peak, t + 0.002); g.gain.exponentialRampToValueAtTime(0.0001, t + len); }
  function noiseSrc (t, len) { const s = ctx.createBufferSource(); s.buffer = noise; s.start(t, Math.random() * 0.5); s.stop(t + len + 0.05); return s; }
  function trigger (r, t, locked) {
    if (!ctx) return;
    const tr = tracks[r];
    const semis = tr.pitch + (locked ? 7 : 0), pf = Math.pow(2, semis / 12);
    const dk = (tr.decay / 127) * (locked ? 0.5 : 1);
    if (r === 0) {
      const o = ctx.createOscillator(), g = ctx.createGain(), len = 0.12 + dk * 0.7;
      o.frequency.setValueAtTime(160 * pf, t); o.frequency.exponentialRampToValueAtTime(46 * pf, t + 0.09);
      env(g, t, 1, len); o.connect(g); g.connect(out); o.start(t); o.stop(t + len + 0.05);
    } else if (r === 1) {
      const len = 0.07 + dk * 0.35, s = noiseSrc(t, len), f = ctx.createBiquadFilter(), g = ctx.createGain();
      f.type = 'highpass'; f.frequency.value = 1400 * pf; env(g, t, 0.7, len); s.connect(f); f.connect(g); g.connect(out);
      const o = ctx.createOscillator(), og = ctx.createGain(); o.type = 'triangle'; o.frequency.setValueAtTime(200 * pf, t); o.frequency.exponentialRampToValueAtTime(140 * pf, t + 0.06);
      env(og, t, 0.6, 0.09); o.connect(og); og.connect(out); o.start(t); o.stop(t + 0.15);
    } else if (r === 2) {
      const len = 0.025 + dk * 0.3, s = noiseSrc(t, len), f = ctx.createBiquadFilter(), g = ctx.createGain();
      f.type = 'highpass'; f.frequency.value = Math.min(16000, 7000 * pf); env(g, t, 0.45, len); s.connect(f); f.connect(g); g.connect(out);
    } else {
      const len = 0.1 + dk * 0.4, s = noiseSrc(t, len + 0.04), f = ctx.createBiquadFilter(), g = ctx.createGain();
      f.type = 'bandpass'; f.frequency.value = 1100 * pf; f.Q.value = 1.2;
      g.gain.setValueAtTime(0.0001, t);
      for (const k of [0, 0.011, 0.022]) { g.gain.exponentialRampToValueAtTime(0.8, t + k + 0.001); g.gain.exponentialRampToValueAtTime(0.1, t + k + 0.009); }
      g.gain.exponentialRampToValueAtTime(0.7, t + 0.031); g.gain.exponentialRampToValueAtTime(0.0001, t + 0.031 + len);
      s.connect(f); f.connect(g); g.connect(out);
    }
    const lag = Math.max(0, (t - ctx.currentTime) * 1000);
    setTimeout(() => { nameBtns[r].classList.add('hit'); setTimeout(() => nameBtns[r].classList.remove('hit'), 90); }, lag);
  }
  let sched = 0;
  function tick () {
    const bpm = Math.max(60, Math.min(200, +$('#bpm').value || 118)), dur = 60 / bpm / 4;
    while (nextT < ctx.currentTime + 0.12) {
      sched = (sched + 1) % 16;
      const s = sched, t = nextT;
      tracks.forEach((tr, r) => { if (tr.steps[s]) trigger(r, t, tr.steps[s] === 2); });
      setTimeout(() => { if (playing) { step = s; paint(); } }, Math.max(0, (t - ctx.currentTime) * 1000));
      nextT += dur;
    }
  }
  const play = $('#play');
  play.addEventListener('click', () => {
    ensureAudio(); if (!ctx) return;
    playing = !playing;
    play.classList.toggle('on', playing); play.querySelector('span').textContent = playing ? 'Stop' : 'Play';
    if (playing) { sched = -1; nextT = ctx.currentTime + 0.05; tick(); timer = setInterval(tick, 25); }
    else { clearInterval(timer); step = -1; paint(); }
  });
  select(0);
})();

/* the screenshots, with a pinball title between them ------------------------------------------------------------------------ */
(function shots () {
  const img = $('#shot'), wipe = $('#wipe'), cap = $('#shotcap'); if (!img) return;
  const caps = {
    son: ['SON', "2 oscillateurs, 1 filtre, 2 enveloppes, 2 LFO. Tout à l'écran, en même temps.",
          "La page Son : deux oscillateurs, l'ampli, deux enveloppes, le filtre, le mix et le crack, la sortie, deux LFO."],
    kit: ['KIT', '24 sons : volume, pan, sortie. Et les 3 triggers de vélocité.', 'La page Kit : 24 sons, chacun avec son volume, son pan et sa sortie.'],
    sequenceur: ['SÉQUENCEUR', "16 pas par son, des verrous, des notes. L'accès rapide juste au-dessus.",
                 'La page Séquenceur : 24 sons, l’accès rapide, l’écran du son, et 16 pas.'],
  };
  $$('#shots button').forEach(b => b.addEventListener('click', () => {
    const k = b.dataset.shot;
    $$('#shots button').forEach(x => x.setAttribute('aria-selected', x === b));
    wipe.querySelector('span').textContent = caps[k][0];
    wipe.classList.remove('go'); void wipe.offsetWidth; wipe.classList.add('go');
    setTimeout(() => { img.src = 'img/' + k + '-1360.webp'; img.alt = caps[k][2]; cap.textContent = caps[k][1]; }, still ? 0 : 160);
  }));
  wipe.addEventListener('animationend', e => { if (e.target === wipe) wipe.classList.remove('go'); });
})();

/* the ghosts at work: a conveyor of things to measure ------------------------------------------------------------------------- */
(function belt () {
  const cv = $('#ghosts'); if (!cv) return;
  const g = cv.getContext('2d'), W = cv.width, H = cv.height, p = 4;
  const objects = [
    ['...+...+....', '....+...+...', '...+...+....', '............', '#########...', '#-------###.', '#-------#..#', '#-------#..#', '#-------###.', '.#-----#....', '..#####.....', '############'],
    ['..######....', '..#++++#....', '..######....', '..#----#....', '..#----#....', '..#----#....', '..#----#....', '..#----##...', '.#------##..', '#---------#.', '#--------##.', '.########...'],
    ['..........#.', '.........##.', '.........#+.', '........#++.', '........#++.', '.......#++#.', '#.....#++#..', '##...#++#...', '.##.#++##...', '..###+##....', '...####.....', '............'],
    ['.##......##.', '#++#....#++#', '.#.######.#.', '..#------#..', '.#---#----#.', '.#---#----#.', '.#---###--#.', '.#--------#.', '..#------#..', '...######...', '..#......#..', '.##......##.'],
    ['.....#......', '..#..#..#...', '...#.#.#....', '#...###...#.', '.#..#.#..#..', '..##..#.##..', '......#.....', '..########..', '...#++++#...', '...#++++#...', '...#++++#...', '....####....'],
    ['............', '....####....', '...#----#...', '...#-k--#++.', '...#----#+++', '....#--#....', '.##.#---#...', '#--#-----#..', '#---------#.', '#---------#.', '.#-------#..', '..#######...'],
    ['............', '############', '#----------#', '#-########-#', '#-#+#..#+#-#', '#-########-#', '#----------#', '#--######--#', '#-#......#-#', '############', '............', '............'],
    ['............', '............', '############', '#----------#', '#-######-+-#', '#-#kkkk#---#', '#-######-+-#', '#----------#', '#-+-+-+-+--#', '############', '.#........#.', '............'],
  ];
  const tools = [
    ['..#####..', '.#-----#.', '#-------#', '#-------#', '#-------#', '.#-----#.', '..#####+.', '.......++', '........+'],
    ['##############', '#+#+#+#+#+#+##', '##############'],
    ['#...#', '#...#', '#...#', '#...#', '.###.', '..#..', '..#..', '..#..'],
  ];
  const readouts = [['25 CL', '1 CAFÉ', '440 HZ'], ['TAILLE 43', '1 SUR 2', 'ODEUR 7/10'], ['1 BANANE', '120 BPM', 'COURBE 31'], ['7 H 00', '60 BPM', 'EN RETARD'],
    ['SOIF 80 %', '3 FEUILLES', '50 HZ'], ['COIN 1,2 KHZ', '100 % CANARD', 'FLOTTE'], ['FACE A', '90 MIN', '-40 DB'], ['MIDRONOME ?!', 'MIDRONOME ?!', 'MIDRONOME ?!']];
  const stamps = ['OK', 'VU', 'OK !', 'BON'];
  const phrases = ['Va te faire un café.', "T'as pas envie d'une bière ?", 'Il faut que tu ailles me chercher du poulet.', "Est-ce que t'as vu dehors ?",
    "T'as pas vu ton midronome ?", "T'es sûr que le midronome n'est pas sous le carton ?", 'Je mesure, je mesure... Toi, tu bouges pas.',
    "C'est le moment d'arroser les plantes.", "Si ça siffle, c'est normal. Enfin, je crois.", 'Une petite pause pipi ? Je garde la machine.', "J'en suis à trois bananes et demie."];
  const kindOf = i => (i % 9 === 5 ? 7 : (i * 5 + 2) % 7);
  const SP = 36 * p, beltY = 236, ghostX = 300;
  let belt = W - 420, last = 0, t = 0, inspecting = -1, inspectT = 0, done = new Set(), phrase = 0, phraseT = 0, raf = 0;
  const xOf = i => W + 60 + i * SP - belt;        // where object i is
  function tag (text, cx, y, scale, stamp) {
    g.save(); g.translate(cx, y); g.scale(scale, scale);
    g.font = stamp ? '700 16px Silk' : '24px VT'; g.textAlign = 'center'; g.textBaseline = 'middle';
    const w = g.measureText(text).width + 16, h = stamp ? 26 : 24;
    if (stamp) { g.rotate(-0.12); g.strokeStyle = text === 'NON...' ? '#ff3b2f' : '#fff'; g.lineWidth = 4; g.fillStyle = '#000'; g.fillRect(-w / 2, -h / 2, w, h); g.strokeRect(-w / 2, -h / 2, w, h); g.fillStyle = g.strokeStyle; }
    else { g.fillStyle = '#fff'; g.fillRect(-w / 2, -h / 2, w, h); g.fillStyle = '#000'; }
    g.fillText(text, 0, 1); g.restore();
  }
  function frame (now) {
    const dt = Math.min(0.05, (now - (last || now)) / 1000); last = now; t += dt;
    if (inspecting < 0) {
      belt += dt * 70;
      const first = Math.max(0, Math.floor((belt - W - 60) / SP));
      for (let i = first; i < first + 12; ++i) {
        const ox = xOf(i) + 24;
        const d = ox - ghostX - 36;
        if (!done.has(i) && d <= 0 && d > -10) { inspecting = i; inspectT = 0; break; }
      }
    } else {
      inspectT += dt;
      if (inspectT > 2.6) { done.add(inspecting); inspecting = -1; }
    }
    phraseT += dt; if (phraseT > 5.5) { phraseT = 0; phrase = (phrase + 1) % phrases.length; }

    g.fillStyle = '#000'; g.fillRect(0, 0, W, H);
    g.fillStyle = '#0d0d0d'; for (let y = 0; y < beltY; y += 8) for (let x = (y / 8) % 2 ? 4 : 0; x < W; x += 8) g.fillRect(x, y, 2, 2);
    // the belt
    g.fillStyle = '#fff'; g.fillRect(0, beltY, W, 4); g.fillRect(0, beltY + 24, W, 4);
    g.fillStyle = '#3a3a3a'; for (let x = -(belt % 32); x < W; x += 32) g.fillRect(x, beltY + 8, 16, 12);
    g.fillStyle = '#6f6f6f'; for (let x = 20; x < W; x += 120) { g.fillRect(x, beltY + 28, 8, 20); }
    // the things
    const first = Math.max(0, Math.floor((belt - W - 80) / SP));
    for (let i = first; i < first + 12; ++i) {
      const x = xOf(i), k = kindOf(i);
      if (x < -60 || x > W + 10) continue;
      sprite(g, objects[k], x, beltY - 12 * p, p);
      const stamped = done.has(i) || (i === inspecting && inspectT > 1.5);
      if (i === inspecting && inspectT > 0.5 || stamped) tag(readouts[k][Math.floor(i / 3) % 3], x + 24, beltY - 12 * p - 26, 1, false);
      if (stamped) {
        const since = i === inspecting ? inspectT - 1.5 : 1;
        tag(k === 7 ? 'NON...' : stamps[i % 4], x + 24, beltY - 22, since < 0.15 ? 1.8 - since / 0.15 * 0.8 : 1, true);
      }
    }
    // the ghost and its tool
    const looking = inspecting >= 0;
    let bob = Math.sin(t * 2.4) * 1.5 * p; if (looking) bob += 2 * p * Math.min(1, inspectT * 4);
    const gx = ghostX - 32, gy = beltY - 12 * p - 6 * p - 16 * p - 20 + bob;
    ghost(g, gx, gy, p, t, { looking, gx: looking ? 1 : 0, gy: looking ? 1 : 0 });
    if (looking) {
      const s = tools[inspecting % 3], tw = s[0].length * p, th = s.length * p;
      const ox = xOf(inspecting) + 24 - tw / 2, oy = inspecting % 3 === 1 ? beltY - 12 * p - th - p : beltY - 10 * p;
      const hx = gx + 60, hy = gy + 40;
      let a = Math.min(1, inspectT / 0.35); if (inspectT > 2.1) a = Math.max(0, 1 - (inspectT - 2.1) / 0.3);
      const wob = inspecting % 3 === 0 && a >= 1 ? Math.sin(inspectT * 12) * p : 0;
      sprite(g, s, hx + (ox - hx) * a + wob, hy + (oy - hy) * a, p);
    }
    // what it says
    const text = phrases[phrase], shown = text.slice(0, Math.floor(phraseT * 28));
    g.font = '26px VT'; g.textBaseline = 'middle';
    const bw = Math.max(80, g.measureText(text).width + 24), bx = Math.min(W - bw - 8, ghostX + 54), by = 26;
    g.fillStyle = '#fff'; g.fillRect(bx, by, bw, 34); g.fillRect(ghostX + 30, by + 34, 8, 8); g.fillRect(ghostX + 22, by + 42, 8, 8);
    g.fillStyle = '#000'; g.fillRect(bx + 3, by + 3, bw - 6, 28);
    g.fillStyle = '#fff'; g.fillText(shown, bx + 12, by + 18);
    if (!still) raf = requestAnimationFrame(frame);
  }
  Promise.all([document.fonts.load('26px VT'), document.fonts.load('700 16px Silk')]).then(() =>
    whenVisible(cv, () => { last = 0; cancelAnimationFrame(raf); raf = requestAnimationFrame(frame); }, () => cancelAnimationFrame(raf)));
})();

/* the update button ---------------------------------------------------------------------------------------------------------------- */
(function upd () {
  const box = $('#upd'), ub = $('#ub'), cur = $('#ucur'); if (!box) return;
  let gen = 0;
  async function run (my) {
    const ok = () => my === gen;
    while (ok()) {
      ub.className = 'ub'; ub.textContent = 'Mettre à jour';
      cur.style.transition = 'none'; cur.style.transform = `translate(${box.clientWidth * 0.35}px, 80px)`; void cur.offsetWidth; cur.style.transition = '';
      await sleep(700); if (!ok()) return;
      ub.classList.add('show');
      for (let i = 0; i < 3; ++i) { ub.classList.add('dim'); await sleep(220); ub.classList.remove('dim'); await sleep(220); }
      const b = ub.getBoundingClientRect(), r = box.getBoundingClientRect();
      cur.style.transform = `translate(${b.left - r.left + b.width / 2}px, ${b.top - r.top + b.height / 2}px)`;
      await sleep(1000); if (!ok()) return;
      cur.classList.remove('click'); void cur.offsetWidth; cur.classList.add('click');
      for (const d of ['.', '..', '...']) { ub.textContent = 'Je charge' + d; await sleep(260); }
      ub.classList.add('rs'); ub.textContent = 'Redémarrer'; await sleep(1800);
    }
  }
  if (still) { ub.classList.add('show'); return; }
  whenVisible(box, () => { run(++gen); }, () => { ++gen; });
})();
})();
