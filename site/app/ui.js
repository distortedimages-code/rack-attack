// The web version's screens: the Séquenceur and Son pages of the plug-in, drawn the same way, on the same engine.
import { Engine } from './engine.js';
import { SECTIONS, COLUMNS, isBipolar, valueText, noteName, DIVISIONS, CONDITIONS, quickFor, TRIGS, fit as fitText } from './params.js';

const E = new Engine();
window.raEngine = E;   // for the console, and tests
const S = { page: 'seq', track: 0, section: 3, sel: new Set(), held: new Map(), stepPage: 0, lastNote: 60, undo: null, undoWhat: '', clip: null, scale: 1 };
const stage = document.getElementById('stage');
const FIELD = { mode: 0, length: 1, division: 2, mute: 3, active: 7, trigs: 8, locks: 9 };

//==============================================================================
// little helpers
function el (tag, cls, parent, text) {
  const e = document.createElement(tag);
  if (cls) e.className = cls;
  if (text != null) e.textContent = text;
  if (parent) parent.appendChild(e);
  return e;
}
const place = (e, x, y, w, hgt) => { e.style.left = x + 'px'; e.style.top = y + 'px'; if (w != null) e.style.width = w + 'px'; if (hgt != null) e.style.height = hgt + 'px'; return e; };
const two = n => String(n).padStart(2, '0');
const upper = s => s.toUpperCase();

function fit () {
  const k = Math.min(innerWidth / 1360, innerHeight / 848);
  S.scale = k;
  stage.style.transform = `translate(${Math.round((innerWidth - 1360 * k) / 2)}px, ${Math.round((innerHeight - 848 * k) / 2)}px) scale(${k})`;
}
addEventListener('resize', fit);
fit();

// iPad / iPhone: no zoom by double tap or pinch (the screen has its own scale), and a way back if it happened anyway
document.addEventListener('gesturestart', e => e.preventDefault());
document.addEventListener('gesturechange', e => e.preventDefault());
let lastTouchEnd = 0;
document.addEventListener('touchend', e => {
  const now = Date.now();
  if (now - lastTouchEnd < 350 && e.changedTouches.length === 1 && !(e.target.closest && e.target.closest('select, input'))) {
    e.preventDefault();                        // no double-tap zoom; the tap still counts
    const t = e.target.closest && e.target.closest('button');
    if (t) t.click();
  }
  lastTouchEnd = now;
}, { passive: false });
const unzoom = document.createElement('button');
unzoom.className = 'unzoom'; unzoom.textContent = 'DÉZOOMER';
document.body.appendChild(unzoom);
unzoom.addEventListener('click', () => {
  const m = document.querySelector('meta[name=viewport]'), c = m.content;
  m.content = c.replace('maximum-scale=1,', 'maximum-scale=1.01,');
  setTimeout(() => { m.content = c; }, 60);
});
if (window.visualViewport) {
  const z = () => { unzoom.style.display = visualViewport.scale > 1.05 ? 'block' : 'none'; };
  visualViewport.addEventListener('resize', z); z();
}

// stage coordinates of an element (for menus and popups)
function boxOf (e) {
  const r = e.getBoundingClientRect(), s = stage.getBoundingClientRect();
  return { x: (r.left - s.left) / S.scale, y: (r.top - s.top) / S.scale, w: r.width / S.scale, h: r.height / S.scale };
}

// a value dragged up and down (mouse, finger, wheel); a tap; a double tap resets
function dragValue (e, o) {
  let y0 = 0, v0 = 0, moved = false, lastTap = 0, acc = 0, id = null, lo = 0, hi = 127;
  e.addEventListener('pointerdown', ev => {
    if (ev.button > 0) return;
    ev.preventDefault();
    id = ev.pointerId; e.setPointerCapture(id);
    y0 = ev.clientY; v0 = o.get(); moved = false;
    lo = typeof o.min === 'function' ? o.min() : o.min;
    hi = typeof o.max === 'function' ? o.max() : o.max;
    o.start && o.start();
  });
  e.addEventListener('pointermove', ev => {
    if (id !== ev.pointerId) return;
    const dy = (y0 - ev.clientY) / S.scale;
    if (!moved && Math.abs(dy) < 4) return;
    moved = true;
    const fine = ev.metaKey || ev.ctrlKey;     // ⌘ or Ctrl: fine steps (Shift is for locks)
    const range = hi - lo, sens = (o.sens || 160) * (fine ? 4 : 1) * (range < 20 ? 20 / Math.max(1, range) * 0.35 : 1);
    const v = Math.max(lo, Math.min(hi, v0 + dy * range / sens));
    o.set(o.float ? v : Math.round(v));
  });
  const up = ev => {
    if (id !== ev.pointerId) return;
    id = null;
    if (!moved) {
      const now = performance.now();
      if (o.reset != null && now - lastTap < 320) { o.set(typeof o.reset === 'function' ? o.reset() : o.reset); lastTap = 0; }
      else { lastTap = now; o.tap && o.tap(ev); }
    }
    o.end && o.end();
  };
  e.addEventListener('pointerup', up);
  e.addEventListener('pointercancel', up);
  e.addEventListener('wheel', ev => {
    ev.preventDefault();
    acc += ev.deltaY;
    if (Math.abs(acc) >= 40) {
      const mn = typeof o.min === 'function' ? o.min() : o.min, mx = typeof o.max === 'function' ? o.max() : o.max;
      o.set(Math.max(mn, Math.min(mx, o.get() - Math.sign(acc) * (o.wheelStep || 1)))); acc = 0; o.end && o.end();
    }
  }, { passive: false });
}

// a ring knob that fills in white from the start, or from the middle when the setting goes both ways
const SVGNS = 'http://www.w3.org/2000/svg';
function ring (parent, size = 40) {
  const s = document.createElementNS(SVGNS, 'svg');
  s.setAttribute('viewBox', '0 0 44 44');
  s.innerHTML = '<circle class="ring-track" cx="22" cy="22" r="17"/><path class="ring-fill"/><rect class="ring-mid" x="21" y="0" width="2" height="5" style="display:none"/><rect class="ring-dot" width="4" height="4"/>';
  s.style.width = s.style.height = size + 'px';
  parent.appendChild(s);
  return s;
}
function setRing (s, frac, centred) {
  const lim = Math.PI * 0.75, a = -lim + Math.max(0, Math.min(1, frac)) * 2 * lim, from = centred ? 0 : -lim;
  const p = t => [22 + 17 * Math.sin(t), 22 - 17 * Math.cos(t)];
  const a0 = Math.min(from, a), a1 = Math.max(from, a);
  const [x0, y0] = p(a0), [x1, y1] = p(a1);
  s.children[1].setAttribute('d', a1 - a0 < 0.01 ? '' : `M${x0.toFixed(2)} ${y0.toFixed(2)} A17 17 0 ${a1 - a0 > Math.PI ? 1 : 0} 1 ${x1.toFixed(2)} ${y1.toFixed(2)}`);
  s.children[2].style.display = centred ? '' : 'none';
  s.children[3].setAttribute('x', (22 + 10 * Math.sin(a) - 2).toFixed(1));
  s.children[3].setAttribute('y', (22 - 10 * Math.cos(a) - 2).toFixed(1));
}

// a menu like the plug-in's: items { id, label, ticked, disabled } or { header } or { sep }
function menu (items, x, y) {
  return new Promise(resolve => {
    const cover = el('div', 'cover', stage);
    const m = el('div', 'menu', stage);
    for (const it of items) {
      if (it.header) { el('div', 'hd', m, upper(it.header)); continue; }
      if (it.sep) { el('div', 'sp', m); continue; }
      const b = el('button', it.ticked ? 'ticked' : '', m);
      el('i', 'tick', b);
      el('span', '', b, it.label);
      b.disabled = !!it.disabled;
      b.addEventListener('click', () => done(it.id));
    }
    const close = () => { cover.remove(); m.remove(); };
    const done = id => { close(); resolve(id); };
    cover.addEventListener('pointerdown', () => done(null));
    m.style.left = Math.max(4, Math.min(1360 - m.offsetWidth - 4, x)) + 'px';
    m.style.top = Math.max(4, Math.min(848 - m.offsetHeight - 4, y)) + 'px';
  });
}

let toastEl = null, toastTimer = 0;
function toast (text, colour = '') {
  if (!toastEl) toastEl = el('div', 'toast', stage);
  toastEl.className = 'toast ' + colour;
  toastEl.textContent = upper(text);
  toastEl.style.opacity = 1;
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => { toastEl.style.opacity = 0; }, 1400);
}

// the big value of the setting being turned (yellow for a lock, with the sound's own value marked)
let popEl = null, popUntil = 0;
function pop (anchor, label, text, frac, centred, locked, baseFrac) {
  if (!popEl) {
    popEl = el('div', 'pop', stage);
    el('div', 'lab', popEl); el('div', 'val', popEl);
    const bar = el('div', 'bar', popEl); el('i', '', bar); el('u', '', bar);
  }
  const b = boxOf(anchor);
  popEl.className = 'pop' + (locked ? ' lk' : '');
  place(popEl, Math.max(8, Math.min(1352 - 300, b.x + b.w / 2 - 150)), Math.max(60, b.y - 150));
  popEl.children[0].textContent = upper(label);
  popEl.children[1].textContent = text;
  const bar = popEl.children[2], f = Math.max(0, Math.min(1, frac));
  const i = bar.children[0], u = bar.children[1];
  if (centred) { i.style.left = Math.min(f, 0.5) * 100 + '%'; i.style.width = Math.abs(f - 0.5) * 100 + '%'; }
  else { i.style.left = '0'; i.style.width = f * 100 + '%'; }
  u.style.display = locked && baseFrac != null ? '' : 'none';
  if (baseFrac != null) u.style.left = baseFrac * 100 + '%';
  popEl.style.display = 'block';
  popEl.style.opacity = 1;
  popUntil = performance.now() + 900;
}

// the steps the settings lock: those held down (a finger each), else those chosen with Shift held
function lockTargets () { return S.held.size ? [...new Set(S.held.values())] : [...S.sel]; }
function lockedByHold (targets) { for (const d of sdrags.values()) if (targets.includes(d.idx)) d.locked = true; }

//==============================================================================
// the undo of OPTIONS
function keepForUndo (what) { S.undo = E.patternBytes(); S.undoWhat = what; }
function undo () { if (!S.undo) return; const back = S.undo; keepForUndo('annuler'); E.loadPattern(back); toast('Annulé'); }

// a whole track, as values (copy / paste)
function readTrack (t) {
  const tr = { fields: [0, 1, 2, 3, 4, 5, 6].map(f => E.track(t, f)), steps: [] };
  for (let s = 0; s < 128; ++s) tr.steps.push({ f: [0, 1, 2, 3, 4, 5, 6, 7, 8].map(f => E.step(t, s, f)), locks: E.locksAt(t, s).map(a => [a, E.lock(t, s, a)]) });
  return tr;
}
function writeTrack (t, tr) {
  const x = E.x;
  [0, 1, 2, 3, 4, 5, 6].forEach(f => x.ra_track_set(t, f, tr.fields[f]));
  tr.steps.forEach((st, s) => {
    st.f.forEach((v, f) => x.ra_step_set(t, s, f, v));
    for (const a of E.locksAt(t, s)) x.ra_lock_clear(t, s, a);
    for (const [a, v] of st.locks) x.ra_lock_set(t, s, a, v);
  });
  E.edited();
}

//==============================================================================
// the top bar
const top = el('div', 'abs top', stage);
el('div', 'logo', top).innerHTML = 'RACK<i>·</i>ATTACK';
const pageKeys = {};
for (const [id, label] of [['seq', 'Séquenceur'], ['son', 'Son']]) {
  const b = el('button', 'key', top, label);
  b.addEventListener('click', () => showPage(id));
  pageKeys[id] = b;
}
const light = el('div', 'light', top);
el('b', '', light);
const lightText = el('span', '', light);
const audioKey = el('button', 'key', top, 'Audio');
audioKey.addEventListener('click', audioDialog);
const midiKey = el('button', 'key', top, 'MIDI');
midiKey.addEventListener('click', midiDialog);
const fullKey = el('button', 'key', top, 'Plein écran');
fullKey.addEventListener('click', () => {
  const d = document.documentElement;
  if (document.fullscreenElement) document.exitFullscreen();
  else if (d.requestFullscreen) d.requestFullscreen().catch(() => {});
  else toast('Pas de plein écran ici');
});
if (!document.documentElement.requestFullscreen) fullKey.style.display = 'none';

function renderTop () {
  for (const id in pageKeys) pageKeys[id].classList.toggle('on', id === S.page);
  const m = E.midi;
  light.className = 'light' + (m.connected ? ' midi' : '') + (m.error ? ' err' : '');
  if (m.error) lightText.textContent = m.error;
  else if (m.status) lightText.textContent = m.status;
  else if (m.connected) lightText.textContent = `RACKATTACK · SYSEX ${two(m.device)} · CANAL ${m.channel}`;
  else lightText.textContent = 'SIMULATION';
}

//==============================================================================
// the Séquenceur page
const seqPage = el('div', 'abs', stage);
place(seqPage, 0, 0, 1360, 848);

const ghostCv = place(el('canvas', 'abs ghost', seqPage), 16, 72);
ghostCv.width = 56; ghostCv.height = 56;
const kitBox = el('div', 'abs kitname', seqPage);
el('span', 'dim', kitBox, 'KIT');
const kitText = el('span', '', kitBox);

const playKey = el('button', 'abs key play', seqPage);
el('i', '', playKey);
const playText = el('span', '', playKey, 'PLAY');
playKey.addEventListener('click', async () => { await E.toggle(); render(); });

function field (x, w, name) {
  const f = place(el('div', 'abs field', seqPage), x, 76, w);
  el('span', '', f, name);
  return { el: f, value: el('b', '', f) };
}
const bpmF = field(660, 230, 'BPM'), swingF = field(900, 230, 'SWING');
dragValue(bpmF.el, { get: () => E.tempo(), set: v => { E.setTempo(Math.round(v * 10) / 10); renderHeader(); if (E.node) E.node.port.postMessage({ t: 'tempo', bpm: E.tempo() }); },
  min: 40, max: 250, sens: 600, float: true, reset: 120, wheelStep: 1,
  tap: () => { const v = prompt('Tempo (BPM)', E.tempo().toFixed(1)); if (v && +v > 0) { E.setTempo(+v); renderHeader(); } } });
dragValue(swingF.el, { get: () => E.swing(), set: v => { E.setSwing(v); renderHeader(); }, min: 50, max: 75, sens: 200, reset: 50 });

const meter = el('div', 'abs meter', seqPage);
const meterRow = el('div', 'row', meter);
const meterName = el('span', '', meterRow, 'SORTIE');
const meterVal = el('b', '', meterRow);
meterVal.style.fontWeight = 'normal'; meterVal.style.fontSize = '24px';
const meterBar = el('div', 'bar', meter);
for (let i = 0; i < 12; ++i) el('i', '', meterBar);
place(el('div', 'abs dots', seqPage), 16, 130);

// the 24 voices
const voicesEl = el('div', 'abs voices', seqPage);
const voices = [];
for (let i = 0; i < 24; ++i) {
  const b = el('button', 'voice', voicesEl);
  const v = { el: b, num: el('div', 'num', b, two(i + 1)), nm: el('span', 'nm', b), tag: el('span', 'tag', b), lk: el('i', 'lk', b), strip: el('div', 'strip', b) };
  for (let k = 0; k < 16; ++k) el('i', '', v.strip);
  let lp = 0, longDone = false;
  b.addEventListener('click', () => { if (longDone) { longDone = false; return; } selectTrack(i); E.audition(i, S.lastNote); });
  b.addEventListener('contextmenu', ev => { ev.preventDefault(); if (!longDone) voiceMenu(i, b); });
  b.addEventListener('pointerdown', () => { longDone = false; lp = setTimeout(() => { longDone = true; voiceMenu(i, b); }, 600); });
  b.addEventListener('pointerup', () => clearTimeout(lp));
  b.addEventListener('pointerleave', () => clearTimeout(lp));
  voices.push(v);
}

async function voiceMenu (i, b) {
  const box = boxOf(b);
  const muted = E.track(i, FIELD.mute) === 1;
  const id = await menu([{ header: `Son ${two(i + 1)}  ${upper(E.trackName(i))}` }, { id: 1, label: 'Renommer…' }, { id: 2, label: muted ? 'Rétablir le son' : 'Couper (mute)' },
    { id: 3, label: 'Copier la piste' }, { id: 4, label: 'Coller la piste', disabled: !S.clip }, { id: 5, label: 'Init piste : vide, 16 pas, 1/16' }], box.x, box.y + box.h);
  if (id === 1) rename(i);
  if (id === 2) { E.setTrack(i, FIELD.mute, muted ? 0 : 1); }
  if (id === 3) { S.clip = readTrack(i); toast('Piste copiée'); }
  if (id === 4 && S.clip) { keepForUndo('coller'); const n = E.trackName(i); writeTrack(i, S.clip); E.setTrackName(i, n.startsWith('SON ') ? '' : n); toast('Piste collée'); }
  if (id === 5) initTracks(false, i);
  render();
}

function rename (i) {
  const n = prompt('Nom de la piste (16 lettres)', E.text(E.x.ra_track_name(i)));
  if (n != null) { E.setTrackName(i, n.slice(0, 16)); render(); }
}

const pointer = el('div', 'abs pointer', seqPage);
const panel = el('div', 'abs panel', seqPage);

// the sound's section tabs
const tabsEl = el('div', 'abs tabs', panel);
const tabKeys = SECTIONS.map((sec, i) => {
  const b = el('button', 'key', tabsEl, sec.tab);
  b.addEventListener('click', () => { S.section = i; titleFx(sec.tab); renderSound(); });
  return b;
});

// the quick access band
const quickEl = el('div', 'abs quick', panel);
const qh = el('div', 'head', quickEl); qh.innerHTML = 'ACCÈS<br>RAPIDE';
const quickCells = [];
for (let i = 0; i < 8; ++i) {
  const c = el('div', 'qcell', quickEl);
  const q = { el: c, ring: ring(c), lab: el('span', 'lab', c), val: el('span', 'val', c), why: el('span', 'why', c), def: null };
  slotDrag(c, () => q.def);
  quickCells.push(q);
}
const tuneKey = el('button', 'tune', quickEl);
el('span', 'box', tuneKey);
const tuneText = el('span', '', tuneKey);
tuneKey.addEventListener('click', toggleTuned);

function toggleTuned () {
  const t = S.track;
  if (E.sound(t, 0x58) !== 0) { E.setSound(t, 0x58, 0); toast('Son non accordé'); render(); return; }
  // a free Tuned Channel: from 16 down, not the global one, not used by another sound
  const used = new Set();
  for (let s = 0; s < 24; ++s) if (s !== t) used.add(E.sound(s, 0x58));
  let ch = 0;
  for (let c = 16; c >= 1; --c) if (c !== E.midi.channel && !used.has(c)) { ch = c; break; }
  if (!ch) { toast('Plus de canal libre', 'red'); return; }
  E.setSound(t, 0x58, ch);
  toast('Son accordé : canal ' + ch);
  render();
}

// the screen and its section's settings
const vizCv = el('canvas', 'abs viz', panel);
vizCv.width = 520; vizCv.height = 150;
const vtag = el('div', 'abs vtag', panel);
const ptitle = el('div', 'abs ptitle', panel);
const ptitleText = el('span', '', ptitle);
function titleFx (text) {
  ptitleText.textContent = upper(text);
  ptitle.classList.remove('go'); void ptitle.offsetWidth; ptitle.classList.add('go');
}
const paramsEl = el('div', 'abs params', panel);
let paramBoxes = [];

// a setting of the open sound, or its lock on the selected steps
function slotValue (def) {
  let v = E.sound(S.track, def.a);
  const tg = lockTargets();
  if (tg.length) { const l = E.lock(S.track, tg[0], def.a); if (l >= 0) return { v, locked: true, lv: l }; }
  return { v, locked: false, lv: v };
}
function setSlot (def, v, anchor) {
  v = Math.max(0, Math.min(E.paramMax(def.a), Math.round(v)));
  const qi = quickCells.findIndex(q => q.el === anchor);
  if (qi >= 0) S.sticky = { track: S.track, index: qi, def };    // it stays there for this sound
  const tg = lockTargets();
  if (tg.length) {
    let first = false;
    lockedByHold(tg);
    for (const s of tg) { if (E.lock(S.track, s, def.a) < 0) first = true; E.x.ra_lock_set(S.track, s, def.a, v); }
    E.edited();
    if (first) toast('Verrou posé', 'yellow');
  } else E.setSound(S.track, def.a, v);
  const sv = slotValue(def), max = E.paramMax(def.a);
  pop(anchor, def.label, valueText(def.a, sv.lv, E.valueNames(def.a)), sv.lv / max, isBipolar(def.a), sv.locked, sv.v / max);
  renderSound(); renderVoices();
  if (tg.length) renderSeq();
}
function slotDrag (c, getDefNow) {
  // the setting under the finger stays the same for the whole gesture
  let held = null;
  const getDef = () => held || getDefNow();
  dragValue(c, {
    start: () => { held = getDefNow(); },
    end: () => { held = null; },
    get: () => slotValue(getDef()).lv,
    set: v => setSlot(getDef(), v, c),
    min: 0, max: () => E.paramMax(getDef().a), sens: 160,
    reset: () => (isBipolar(getDef().a) ? 64 : slotValue(getDef()).lv),
    tap: () => {
      const d = getDef();
      const names = E.valueNames(d.a);
      if (names.length) choose(d, c);
    },
  });
  c.addEventListener('contextmenu', ev => {
    ev.preventDefault();
    const d = getDef();
    const tg = lockTargets();
    if (tg.length) { for (const s of tg) E.x.ra_lock_clear(S.track, s, d.a); E.edited(); toast('Verrou retiré', 'yellow'); render(); }
  });
}
async function choose (def, anchor) {
  const names = E.valueNames(def.a), cur = slotValue(def).lv, b = boxOf(anchor);
  const id = await menu([{ header: def.label }, ...names.map((n, i) => ({ id: i + 1, label: n, ticked: i === cur }))], b.x, b.y + b.h);
  if (id) setSlot(def, id - 1, anchor);
}

// the open track's sequencer
place(el('div', 'abs sep', panel), 14, 314);
const shead = el('div', 'abs shead', panel);
const tname = el('button', 'tname', shead);
tname.addEventListener('dblclick', () => rename(S.track));
const modeKeys = ['Pas', 'Notes'].map((label, i) => {
  const b = el('button', 'key mode', shead, label);
  b.addEventListener('click', () => {
    if (i === 1 && E.sound(S.track, 0x58) === 0) toast('Son pas accordé : coche Accorde mon son !', 'yellow');
    E.setTrack(S.track, FIELD.mode, i === 1 ? 2 : 0); render();
  });
  return b;
});
function sfield (name, click) {
  const f = el('div', 'sfield' + (click ? ' click' : ''), shead);
  el('span', '', f, name);
  return { el: f, value: el('b', '', f) };
}
const lenF = sfield('LONG.'), divF = sfield('VIT.', true);
dragValue(lenF.el, { get: () => E.track(S.track, FIELD.length), set: v => { E.setTrack(S.track, FIELD.length, v); renderSeq(); renderVoices(); }, min: 1, max: 128, sens: 400, reset: 16 });
divF.el.addEventListener('click', async () => {
  const b = boxOf(divF.el), cur = E.track(S.track, FIELD.division);
  const id = await menu([{ header: 'Vitesse' }, ...DIVISIONS.map((d, i) => ({ id: i + 1, label: d, ticked: i === cur }))], b.x, b.y + b.h);
  if (id) { E.setTrack(S.track, FIELD.division, id - 1); render(); }
});
const muteKey = el('button', 'key', shead, 'Mute');
muteKey.style.width = '96px';
muteKey.addEventListener('click', () => { E.setTrack(S.track, FIELD.mute, E.track(S.track, FIELD.mute) ? 0 : 1); render(); });
const optKey = el('button', 'key', shead, 'Options ▾');
optKey.addEventListener('click', options);
el('div', 'grow', shead);
const initT = el('button', 'key init', shead, 'Init piste');
const initA = el('button', 'key init', shead, 'Init séquence');
initT.addEventListener('click', () => initTracks(false));
initA.addEventListener('click', () => initTracks(true));

function initTracks (all, t = S.track) {
  keepForUndo(all ? 'init séquence' : 'init piste');
  for (let i = 0; i < 24; ++i) if (all || i === t) E.x.ra_track_op(i, 2, 0);
  E.edited();
  S.sel.clear(); S.stepPage = 0;
  toast(all ? 'Séquence initialisée' : 'Piste initialisée', 'red');
  render();
}

async function options () {
  const b = boxOf(optKey), t = S.track;
  const id = await menu([
    { header: `Piste ${two(t + 1)}  ${upper(E.trackName(t))}` },
    { id: 1, label: 'Effacer les pas' }, { id: 2, label: 'Effacer les verrous' }, { id: 3, label: 'Init piste : vide, 16 pas, 1/16' },
    { id: 10, label: 'Remplir : un pas sur 2' }, { id: 11, label: 'Remplir : un pas sur 4' }, { id: 12, label: 'Remplir : un pas sur 8' },
    { id: 4, label: "Décaler d'un pas vers la gauche" }, { id: 5, label: "Décaler d'un pas vers la droite" },
    { id: 6, label: 'Copier la piste' }, { id: 7, label: 'Coller la piste', disabled: !S.clip },
    { header: 'Séquence' },
    { id: 20, label: 'Effacer tous les pas' }, { id: 21, label: 'Effacer tous les verrous' }, { id: 22, label: 'Init séquence : tout vide, 16 pas, 1/16 (le tempo reste)' },
    { sep: true },
    { id: 30, label: S.undo ? 'Annuler : ' + S.undoWhat : 'Annuler', disabled: !S.undo },
  ], b.x, b.y + b.h);
  const onTrack = (what, op, arg = 0) => { keepForUndo(what); E.trackOp(t, op, arg); };
  const onAll = (what, op) => { keepForUndo(what); for (let i = 0; i < 24; ++i) E.x.ra_track_op(i, op, 0); E.edited(); };
  switch (id) {
    case 1: onTrack('effacer les pas', 0); toast('Pas effacés', 'red'); break;
    case 2: onTrack('effacer les verrous', 1); toast('Verrous effacés', 'yellow'); break;
    case 3: initTracks(false); break;
    case 4: onTrack('décaler', 3, -1); toast('<< Décalé'); break;
    case 5: onTrack('décaler', 3, 1); toast('Décalé >>'); break;
    case 6: S.clip = readTrack(t); toast('Piste copiée'); break;
    case 7: if (S.clip) { keepForUndo('coller la piste'); const n = E.text(E.x.ra_track_name(t)); writeTrack(t, S.clip); E.setTrackName(t, n); toast('Piste collée'); } break;
    case 10: case 11: case 12: { const every = id === 10 ? 2 : id === 11 ? 4 : 8; onTrack('remplir', 4, every); toast('Rempli : 1 sur ' + every); break; }
    case 20: onAll('effacer tous les pas', 0); toast('Tout effacé', 'red'); break;
    case 21: onAll('effacer tous les verrous', 1); toast('Tous les verrous effacés', 'yellow'); break;
    case 22: initTracks(true); break;
    case 30: undo(); break;
  }
  if (id) { S.sel.clear(); render(); }
}

// the 16 steps of the open page
const stepsEl = el('div', 'abs steps', panel);
const steps = [];
for (let i = 0; i < 16; ++i) {
  const b = el('div', 'step', stepsEl);
  steps.push({ el: b, n: el('span', 'n', b), note: el('span', 'note', b), velo: el('i', 'velo', b), lks: el('div', 'lks', b), cond: el('span', 'cond', b), rt: el('div', 'rt', b) });
}
const mini = el('div', 'abs mini', panel);
const hint = el('div', 'abs hint', panel);
const hintText = el('span', '', hint, 'clic = poser · glisser = peindre · tirer vers le haut = ratchet · appui long = verrou · appui long sur un son = menu');
const clearSel = el('button', 'key', hint, '✕ Fini les verrous');
clearSel.addEventListener('click', () => { S.sel.clear(); render(); });
const trigsEl = el('div', 'abs trigs', panel);
const trigs = TRIGS.map((name, f) => {
  const b = el('div', 'trig', trigsEl);
  el('span', '', b, upper(name));
  const t = { el: b, value: el('b', '', b) };
  dragValue(b, {
    get: () => trigValue(f), set: v => setTrig(f, v),
    min: [1, 1, -11, 0, 0, 1, 0][f], max: [127, 64, 11, 100, CONDITIONS.length - 1, 8, 127][f], sens: [200, 200, 120, 200, 200, 100, 300][f],
    reset: [100, 4, 0, 100, 0, 1, 60][f],
  });
  return t;
});

function trigValue (f) {
  const tg = lockTargets();
  if (!tg.length) return 0;
  const s = tg[0], t = S.track;
  if (f === 4) { const a = E.step(t, s, 6), b = E.step(t, s, 7); return Math.max(0, CONDITIONS.findIndex(c => c[0] === a && c[1] === b)); }
  return E.step(t, s, [1, 3, 4, 5, 0, 8, 2][f]);
}
function trigText (f) {
  const v = trigValue(f);
  switch (f) {
    case 1: { const e = ['', '1/8', '1/4', '3/8', '1/2', '5/8', '3/4', '7/8']; return v < 8 ? e[v] : (v / 8).toFixed(v % 8 ? 2 : 0).replace(/0+$/, '').replace(/\.$/, ''); }
    case 2: return v > 0 ? '+' + v : '' + v;
    case 3: return v + '%';
    case 4: { const c = CONDITIONS[v]; return c[1] === 0 ? '1:1' : c[0] + ':' + c[1]; }
    case 5: return v <= 1 ? 'NON' : 'x' + v;
    case 6: return noteName(v);
    default: return '' + v;
  }
}
function setTrig (f, v) {
  const tg = lockTargets();
  if (!tg.length) { toast('Garde un doigt sur un pas (ou Maj + clic)', 'yellow'); return; }
  lockedByHold(tg);
  for (const s of tg) {
    if (f === 4) { const c = CONDITIONS[v]; E.x.ra_step_set(S.track, s, 6, c[0]); E.x.ra_step_set(S.track, s, 7, c[1]); }
    else E.x.ra_step_set(S.track, s, [1, 3, 4, 5, 0, 8, 2][f], v);
    if (f === 6) S.lastNote = v;
  }
  E.edited();
  renderSeq();
}

// steps: a tap puts or takes a trig, a drag across paints, up or down makes ratchets; a finger that stays on a
// step holds it: the settings turned meanwhile lock it (several fingers, several steps), letting go ends it,
// as with a held trig on an Elektron. With a mouse: Maj + clic chooses steps while Maj is held.
const sdrags = new Map();      // pointer -> what it does on the steps
stepsEl.addEventListener('pointerdown', ev => {
  const i = steps.findIndex(s => s.el === ev.target.closest('.step'));
  if (i < 0) return;
  ev.preventDefault();
  const idx = S.stepPage * 16 + i, t = S.track;
  if (idx >= E.track(t, FIELD.active)) return;
  if (ev.button === 2) return;
  if (ev.shiftKey) { toggleSel(idx); return; }
  try { stepsEl.setPointerCapture(ev.pointerId); } catch (e) {}
  const notes = E.track(t, FIELD.mode) === 2, on = E.isOn(t, idx);
  sdrags.set(ev.pointerId, { idx, x0: ev.clientX, y0: ev.clientY, painting: false, ratchet: false, base: Math.max(1, E.step(t, idx, 8)),
                             paint: !on, notes: notes && on, note0: E.step(t, idx, 2), moved: false, locked: false });
  S.held.set(ev.pointerId, idx);
  renderSeq(); renderSound();
});
stepsEl.addEventListener('pointermove', ev => {
  const d = sdrags.get(ev.pointerId);
  if (!d || d.locked) return;              // a step that got locks is only held now
  const t = S.track;
  const dy = (d.y0 - ev.clientY) / S.scale, dx = (ev.clientX - d.x0) / S.scale;
  const release = () => { if (S.held.delete(ev.pointerId)) { renderSeq(); renderSound(); } };
  if (d.notes) {
    if (Math.abs(dy) < 6 && !d.moved) return;
    d.moved = true; release();
    const n = Math.max(0, Math.min(127, d.note0 + Math.round(dy / 8)));
    if (n !== E.step(t, d.idx, 2)) { E.setStep(t, d.idx, 2, n); S.lastNote = n; renderSeq(); pop(steps[d.idx % 16].el, 'Note', noteName(n), n / 127, false, false); }
    return;
  }
  const target = document.elementFromPoint(ev.clientX, ev.clientY);
  const j = steps.findIndex(s => s.el === (target && target.closest('.step')));
  // the first clear move decides: up or down on the step = ratchets, across = painting
  if (!d.painting && !d.ratchet && Math.abs(dy) >= 10 && Math.abs(dy) > Math.abs(dx)) {
    d.ratchet = true; d.moved = true; release(); keepForUndo('ratchet');
    if (!E.isOn(t, d.idx)) setOn(d.idx, true);
  }
  if (d.ratchet) {
    const r = Math.max(1, Math.min(8, d.base + Math.round(dy / 14)));
    if (r !== E.step(t, d.idx, 8)) { E.setStep(t, d.idx, 8, r); renderSeq(); }
    pop(steps[d.idx % 16].el, 'Ratchet', r > 1 ? 'x' + r : 'NON', (r - 1) / 7, false, false);
    return;
  }
  if (j < 0) return;
  const idx = S.stepPage * 16 + j;
  if (idx === d.idx && !d.painting) return;
  if (!d.painting) { d.painting = true; d.moved = true; release(); keepForUndo('peindre'); setOn(d.idx, d.paint); }
  if (idx < E.track(t, FIELD.active)) setOn(idx, d.paint);
});
const stepUp = ev => {
  const d = sdrags.get(ev.pointerId);
  if (!d) return;
  sdrags.delete(ev.pointerId);
  S.held.delete(ev.pointerId);
  // a tap (nothing locked, no move): the trig goes on or off
  if (!d.moved && !d.locked && ev.type === 'pointerup') { keepForUndo('poser'); setOn(d.idx, d.notes ? false : d.paint); }
  renderSeq(); renderSound();
};
stepsEl.addEventListener('pointerup', stepUp);
stepsEl.addEventListener('pointercancel', stepUp);
stepsEl.addEventListener('contextmenu', ev => ev.preventDefault());
// Maj let go: out of the lock mode
addEventListener('keyup', ev => { if (ev.key === 'Shift' && S.sel.size) { S.sel.clear(); render(); } });
addEventListener('blur', () => { if (S.sel.size) { S.sel.clear(); render(); } });

function setOn (idx, on) {
  const t = S.track;
  if (E.track(t, FIELD.mode) === 1) E.x.ra_track_op(t, 5, 0);     // euclid: its hits become steps
  E.x.ra_step_set(t, idx, 0, on ? 1 : 0);
  if (on && E.track(t, FIELD.mode) === 2) E.x.ra_step_set(t, idx, 2, S.lastNote);
  E.edited();
  renderSeq(); renderVoices();
}
function toggleSel (idx) {
  if (S.sel.has(idx)) S.sel.delete(idx); else S.sel.add(idx);
  if (navigator.vibrate) navigator.vibrate(10);
  render();
}

function selectTrack (t) {
  if (t !== S.track) { S.sel.clear(); S.stepPage = 0; S.sticky = null; }
  S.track = t;
  render();
}

//==============================================================================
// the Son page: four columns of cards, every setting of the open sound
const sonPage = el('div', 'abs', stage);
place(sonPage, 0, 0, 1360, 848);
const sonHead = el('div', 'abs kitname', sonPage);
place(sonHead, 16, 64);
const sonTitle = el('span', '', sonHead);
const sonBody = place(el('div', 'abs sonpage', sonPage), 16, 116, 1328, 724);
const sonCells = [];
for (const col of COLUMNS) {
  const c = el('div', 'col', sonBody);
  for (const card of col) {
    const k = el('div', 'card', c);
    el('h3', '', k, upper(card.title));
    for (const row of card.rows) {
      const r = el('div', 'krow', k);
      for (const p of row) {
        const cell = el('div', 'cell', r);
        const o = { def: { a: p.a, label: p.label }, el: cell, lab: el('span', 'lab', cell, p.label), ring: ring(cell, 36), val: el('span', 'val', cell), src: null };
        if (p.src >= 0) {
          o.src = el('button', 'src', cell);
          o.srcDef = { a: p.src, label: 'Source' };
          o.src.addEventListener('pointerdown', ev => ev.stopPropagation());
          o.src.addEventListener('click', async ev => {
            ev.stopPropagation();
            const names = E.valueNames(p.src), cur = E.sound(S.track, p.src), b = boxOf(o.src);
            const id = await menu([{ header: p.label + ' : source' }, ...names.map((n, i) => ({ id: i + 1, label: n, ticked: i === cur }))], b.x, b.y + b.h);
            if (id) { E.setSound(S.track, p.src, id - 1); renderSon(); }
          });
        }
        dragValue(cell, {
          get: () => E.sound(S.track, p.a), set: v => { E.setSound(S.track, p.a, v); renderSon(); },
          min: 0, max: () => E.paramMax(p.a), sens: 160, reset: () => (isBipolar(p.a) ? 64 : E.sound(S.track, p.a)),
          tap: async () => {
            const names = E.valueNames(p.a);
            if (!names.length) return;
            const b = boxOf(cell), cur = E.sound(S.track, p.a);
            const id = await menu([{ header: p.label }, ...names.map((n, i) => ({ id: i + 1, label: n, ticked: i === cur }))], b.x, b.y + b.h);
            if (id) { E.setSound(S.track, p.a, id - 1); renderSon(); }
          },
        });
        sonCells.push(o);
      }
    }
  }
}

function renderSon () {
  sonTitle.innerHTML = `<span class="dim">SON ${two(S.track + 1)}</span>  ${upper(E.trackName(S.track))}`;
  for (const o of sonCells) {
    const a = o.def.a, v = E.sound(S.track, a), names = E.valueNames(a), max = names.length ? Math.max(1, names.length - 1) : 127;
    setRing(o.ring, v / max, isBipolar(a));
    o.val.textContent = fitText(valueText(a, v, names), 6);
    o.el.classList.toggle('choice', names.length > 0);
    if (o.src) o.src.textContent = fitText(valueText(o.srcDef.a, E.sound(S.track, o.srcDef.a), E.valueNames(o.srcDef.a)), 6);
  }
}

//==============================================================================
// drawing the state

function render () {
  renderTop(); renderHeader(); renderVoices(); renderSound(); renderSeq();
  if (S.page === 'son') renderSon();
}

function renderHeader () {
  kitText.textContent = upper(E.kitName() || 'non lu');
  playKey.classList.toggle('on', E.playing);
  playText.textContent = E.playing ? 'STOP' : 'PLAY';
  bpmF.value.textContent = E.tempo().toFixed(1);
  swingF.value.textContent = E.swing() + '%';
}

function renderVoices () {
  for (let i = 0; i < 24; ++i) {
    const v = voices[i], muted = E.track(i, FIELD.mute) === 1, mode = E.track(i, FIELD.mode);
    v.el.classList.toggle('sel', i === S.track);
    v.el.classList.toggle('muted', muted);
    v.nm.textContent = upper(E.trackName(i));
    v.tag.className = 'tag' + (muted ? ' mute' : mode === 2 ? ' note' : '');
    v.tag.textContent = muted ? 'MUTE' : mode === 2 ? 'NOTE' : '';
    v.lk.style.display = !muted && mode !== 2 && E.track(i, FIELD.locks) > 0 ? '' : 'none';
    const len = E.track(i, FIELD.active);
    for (let k = 0; k < 16; ++k) v.strip.children[k].className = k < len && E.isOn(i, k) ? 'on' : '';
  }
  const b = voices[S.track].el;
  pointer.style.left = (16 + b.offsetLeft + b.offsetWidth / 2 - 12) + 'px';
}

function renderSound () {
  tabKeys.forEach((k, i) => k.classList.toggle('on', i === S.section));
  // quick access
  const v = a => E.sound(S.track, a);
  const list = quickFor(v);
  const st = S.sticky;
  if (st && st.track === S.track && list[st.index].a !== st.def.a) list[st.index] = { ...st.def, why: '' };
  list.forEach((def, i) => {
    const q = quickCells[i];
    q.def = def;
    const sv = slotValue(def), max = E.paramMax(def.a);
    setRing(q.ring, sv.lv / Math.max(1, max), isBipolar(def.a));
    q.lab.textContent = upper(def.label);
    q.val.textContent = fitText(valueText(def.a, sv.lv, E.valueNames(def.a)), 7);
    q.why.textContent = def.why; q.why.style.display = def.why ? '' : 'none';
    q.el.classList.toggle('lk', sv.locked);
  });
  const ch = v(0x58);
  tuneKey.classList.toggle('on', ch !== 0);
  tuneText.innerHTML = 'ACCORDE<br>MON SON !' + (ch ? `<small>canal ${ch === 17 ? 'global' : ch}</small>` : '');
  // the section's settings
  const sec = SECTIONS[S.section], flat = sec.rows.flat();
  if (paramBoxes.length !== flat.length || paramBoxes.some((b, i) => b.def.a !== flat[i].a)) {
    paramsEl.innerHTML = '';
    const cols = Math.max(...sec.rows.map(r => r.length));
    paramsEl.style.gridTemplateColumns = `repeat(${Math.max(cols, 4)}, 1fr)`;
    paramBoxes = [];
    for (const row of sec.rows) {
      row.forEach(def => {
        const b = el('div', 'pbox' + (def.choice ? ' choice' : ''), paramsEl);
        const o = { def, el: b, lab: el('span', 'lab', b, upper(def.label)), val: el('span', 'val', b), bar: el('div', 'bar', b) };
        el('i', '', o.bar);
        slotDrag(b, () => o.def);
        paramBoxes.push(o);
      });
      for (let k = row.length; k < Math.max(cols, 4); ++k) el('div', '', paramsEl);   // rows start on their own line
    }
  }
  for (const o of paramBoxes) {
    const sv = slotValue(o.def), max = Math.max(1, E.paramMax(o.def.a)), f = sv.lv / max, i = o.bar.children[0];
    o.val.textContent = fitText(valueText(o.def.a, sv.lv, E.valueNames(o.def.a)), 8);
    if (isBipolar(o.def.a)) { i.style.left = Math.min(f, 0.5) * 100 + '%'; i.style.width = Math.max(2, Math.abs(f - 0.5) * 100) + '%'; }
    else { i.style.left = '0'; i.style.width = f * 100 + '%'; }
    o.el.classList.toggle('lk', sv.locked);
  }
  vtag.textContent = upper(sec.tab);
  drawViz();
}

function renderSeq () {
  const t = S.track, mode = E.track(t, FIELD.mode), len = E.track(t, FIELD.active);
  const pages = Math.max(1, Math.ceil(len / 16));
  if (S.stepPage >= pages) S.stepPage = pages - 1;
  tname.innerHTML = `<span class="dim">${two(t + 1)}</span> ${upper(E.trackName(t))}`;
  modeKeys[0].classList.toggle('on', mode === 0);
  modeKeys[1].classList.toggle('on', mode === 2);
  lenF.value.textContent = mode === 1 ? E.track(t, 4) : E.track(t, FIELD.length);
  divF.value.textContent = DIVISIONS[E.track(t, FIELD.division)];
  muteKey.classList.toggle('red', E.track(t, FIELD.mute) === 1);
  const heldSet = new Set(S.held.values());
  for (let i = 0; i < 16; ++i) {
    const idx = S.stepPage * 16 + i, st = steps[i], inside = idx < len;
    st.el.classList.toggle('out', !inside);
    if (!inside) continue;
    const on = E.isOn(t, idx), locks = E.step(t, idx, 9);
    st.el.classList.toggle('on', on);
    st.el.classList.toggle('sel', S.sel.has(idx) || heldSet.has(idx));
    st.n.textContent = idx + 1;
    st.note.textContent = on && mode === 2 ? noteName(E.step(t, idx, 2)) : '';
    st.velo.style.width = on ? Math.round(E.step(t, idx, 1) / 127 * 60) + '%' : '0';
    st.lks.innerHTML = locks ? '<i></i>'.repeat(Math.min(4, locks)) : '';
    const a = E.step(t, idx, 6), b = E.step(t, idx, 7), rt = E.step(t, idx, 8);
    st.cond.textContent = on && b ? a + ':' + b : '';
    st.rt.innerHTML = on && rt > 1 ? '<i></i>'.repeat(rt) : '';
  }
  // the pages of a long track
  if (mini.children.length !== pages) {
    mini.innerHTML = '';
    for (let p = 0; p < pages; ++p) {
      const g = el('div', 'pg', mini);
      for (let k = 0; k < 16; ++k) el('i', '', g);
      g.addEventListener('click', () => { S.stepPage = p; renderSeq(); });
    }
  }
  [...mini.children].forEach((g, p) => {
    g.classList.toggle('open', p === S.stepPage);
    for (let k = 0; k < 16; ++k) { const idx = p * 16 + k; g.children[k].className = idx < len && E.isOn(t, idx) ? 'on' : ''; g.children[k].style.visibility = idx < len ? '' : 'hidden'; }
  });
  mini.style.visibility = pages > 1 ? '' : 'hidden';
  const tg = lockTargets();
  clearSel.style.display = S.sel.size && !S.held.size ? '' : 'none';
  hintText.textContent = S.held.size ? `pas ${tg.map(x => x + 1).join(', ')} tenu${tg.length > 1 ? 's' : ''} : tourne un réglage = verrou (jaune) · DURÉE, VÉLO... en bas · lâche pour finir`
    : S.sel.size ? `${S.sel.size} pas choisis : tourne un réglage pour les verrouiller · lâche Maj pour finir`
    : 'clic = poser · glisser = peindre · tirer vers le haut = ratchet · garder le doigt sur un pas + tourner un réglage = verrou';
  trigs.forEach((tr, f) => { tr.el.classList.toggle('live', tg.length > 0); tr.value.textContent = tg.length ? trigText(f) : '-'; });
}

//==============================================================================
// the sound's screen, drawn by section
function drawViz () {
  const g = vizCv.getContext('2d'), W = vizCv.width, H = vizCv.height, t = S.track, v = a => E.sound(t, a);
  g.fillStyle = '#000'; g.fillRect(0, 0, W, H);
  g.fillStyle = '#161616'; for (let y = 4; y < H; y += 8) for (let x = 4; x < W; x += 8) g.fillRect(x, y, 2, 2);
  g.fillStyle = '#fff'; g.strokeStyle = '#fff'; g.lineWidth = 2;
  const sec = SECTIONS[S.section].tab;
  const line = pts => { g.beginPath(); pts.forEach(([x, y], i) => (i ? g.lineTo(x, y) : g.moveTo(x, y))); g.stroke(); };
  const label = (text, x, y, c = '#9a9a9a') => { g.fillStyle = c; g.font = '22px VT'; g.fillText(text, x, y); };
  const L = 20, R = W - 20, T = 30, B = H - 22;
  const wave = (shape, cycles, amp = 1) => {
    const pts = [];
    let seed = 7;
    const rnd = () => { seed = (seed * 16807) % 2147483647; return seed / 2147483647 * 2 - 1; };
    let held = 0;
    for (let x = L; x <= R; x += 2) {
      const ph = ((x - L) / (R - L) * cycles) % 1;
      let y;
      switch (shape) {
        case 0: y = ph < 0.5 ? ph * 4 - 1 : 3 - ph * 4; break;
        case 1: y = Math.sin(ph * 2 * Math.PI); break;
        case 2: y = ph < 0.5 ? 1 : -1; break;
        case 3: y = 1 - 2 * ph; break;
        case 4: if (ph < 0.08 || x === L) held = rnd(); y = held; break;
        default: y = rnd() * (shape === 5 ? 1 : shape === 6 ? 0.6 : 0.85) * (shape >= 6 ? Math.exp(-(x - L) / (R - L) * (shape === 6 ? 6 : shape === 7 ? 2 : 1)) : 1);
      }
      pts.push([x, (T + B) / 2 - y * amp * (B - T) / 2]);
    }
    line(pts);
  };
  if (sec === 'Osc 1' || sec === 'Osc 2') {
    const base = sec === 'Osc 1' ? 0x01 : 0x10, shape = v(base), pitch = v(base + 1);
    wave(shape, 1 + Math.round(pitch / 32), 0.9);
    label((E.valueNames(base)[shape] || '') + ' · PITCH ' + pitch, L, H - 4);
  } else if (sec === 'Mix') {
    const bars = [['OSC 1', v(0x1A)], ['OSC 2', v(0x1B)], ['RING', v(0x1C)], ['CRACK', v(0x1D)]];
    bars.forEach(([n, x], i) => {
      const bx = L + i * 120, h = Math.round(x / 127 * (B - T));
      g.fillStyle = '#fff'; g.fillRect(bx, B - h, 60, h);
      label(n, bx, H - 4);
    });
  } else if (sec === 'Filtre') {
    const type = v(0x26), cut = v(0x27) / 127, res = v(0x28) / 127, names = E.valueNames(0x26);
    const pts = [];
    for (let x = L; x <= R; x += 2) {
      const f = (x - L) / (R - L), d = (f - cut) * 8, peak = res * 0.8 * Math.exp(-d * d * 4);
      const lp = 1 / (1 + Math.exp(d * 1.5)), hp = 1 - lp;
      let y = [1, lp, Math.exp(-d * d), hp, 1 - Math.exp(-d * d) * 0.95, f < cut ? 1 : 0.5, 0.5 + 0.5 * Math.exp(-d * d), lp][Math.min(type, 7)] ?? lp;
      y = Math.min(1.4, y + (type >= 1 && type <= 3 ? peak : 0));
      pts.push([x, B - y * (B - T) * 0.7]);
    }
    line(pts);
    label((names[type] || '') + ' · CUTOFF ' + v(0x27) + ' · RESO ' + v(0x28), L, H - 4);
  } else if (sec === 'Env 1' || sec === 'Env 2') {
    const b = sec === 'Env 1' ? 0x3F : 0x45, a = v(b), d = v(b + 1), s = v(b + 2) / 127, r = v(b + 3);
    const total = a + d + 40 + r + 1, k = (R - L) / total;
    let x = L;
    const pts = [[x, B]]; x += a * k; pts.push([x, T]); x += d * k; pts.push([x, B - s * (B - T)]); x += 40 * k; pts.push([x, B - s * (B - T)]); x += r * k; pts.push([x, B]);
    line(pts);
    label(`A ${a} · D ${d} · S ${v(b + 2)} · R ${r}`, L, H - 4);
  } else if (sec === 'LFO 1' || sec === 'LFO 2') {
    const b = sec === 'LFO 1' ? 0x4B : 0x4F, shape = v(b + 1), names = E.valueNames(b + 1);
    wave(Math.min(shape, 4), 1 + Math.round(v(b) / 24), 0.8);
    label((names[shape] || '') + ' · SPEED ' + v(b), L, H - 4);
  } else if (sec === 'Ampli') {
    const vol = v(0x35) / 127, pan = v(0x3A);
    g.fillStyle = '#fff'; g.fillRect(L, T + 20, Math.round(vol * (R - L)), 24);
    g.fillRect(L + Math.round(pan / 127 * (R - L)) - 4, T + 60, 8, 24);
    label('VOLUME ' + v(0x35) + ' · PAN ' + valueText(0x3A, pan), L, H - 4);
  } else {
    label('SORTIE ' + valueText(0x55, v(0x55), E.valueNames(0x55)) + ' · FX ' + valueText(0x56, v(0x56), E.valueNames(0x56)), L, T + 20, '#fff');
    label('ENVOI ' + valueText(0x57, v(0x57)) + ' · XOR ' + valueText(0x5E, v(0x5E), E.valueNames(0x5E)), L, T + 50, '#fff');
    if (v(0x6C)) label('PATTERN INTERNE : ' + valueText(0x6C, v(0x6C), E.valueNames(0x6C)), L, T + 110, '#ffb000');
    label('TUNED ' + valueText(0x58, v(0x58), E.valueNames(0x58)) + ' · TRANSP ' + valueText(0x59, v(0x59)), L, T + 80, '#fff');
  }
}

//==============================================================================
// the mascot, the flashes, the playhead: every frame
const BODY = ['.....######.....', '...##########...', '..############..', '.##############.', '.##############.', '.##############.', '################', '################',
  '################', '################', '################', '###############+', '##############++', '#############+++'];
const SKIRT = [['##.####..####.##', '#...##....##...#'], ['#.####..####.###', '...##....##...##']];
function drawGhost (cv, p, t, lift, blink) {
  const g = cv.getContext('2d');
  g.clearRect(0, 0, cv.width, cv.height);
  const y0 = 4 - lift;
  const put = (rows, yy) => rows.forEach((r, ry) => [...r].forEach((c, rx) => { if (c === '.') return; g.fillStyle = c === '#' ? '#fff' : '#9a9a9a'; g.fillRect(rx * p, yy + ry * p, p, p); }));
  put(BODY, y0 + 4); put(SKIRT[Math.floor(t * 4) % 2], y0 + 4 + 14 * p);
  g.fillStyle = '#000';
  for (const e of [4, 10]) blink ? g.fillRect(e * p, y0 + 4 + 6 * p, 2 * p, p) : g.fillRect(e * p, y0 + 4 + 4 * p, 2 * p, 3 * p);
  g.fillRect(7 * p, y0 + 4 + 9 * p, 2 * p, 2 * p);
}

let lastBeat = -1;
function frame () {
  const now = performance.now(), ppq = E.playhead();
  // voices flash when they play
  for (let i = 0; i < 24; ++i) {
    const age = now - E.hits[i];
    voices[i].el.classList.toggle('flash', age < 70);
    voices[i].el.classList.toggle('flash2', age >= 70 && age < 220);
  }
  if (S.page === 'seq') {
    const up = ppq >= 0 && (ppq % 1) < 0.5;
    drawGhost(ghostCv, 3, now / 1000, up ? 4 : 0, (now % 4200) < 140);
    // the playhead of the open track
    const t = S.track, spb = [8, 4, 2, 1, 6, 3][E.track(t, FIELD.division)], len = E.track(t, FIELD.active);
    const cur = ppq >= 0 ? Math.floor(ppq * spb) % len : -1;
    const frac = ppq >= 0 ? ppq * spb - Math.floor(ppq * spb) : 0, stepMs = 60000 / E.tempo() / spb;
    steps.forEach((s, i) => {
      const idx = S.stepPage * 16 + i, back = cur >= 0 ? ((cur - idx) % len + len) % len : -1;
      const since = back > 0 ? (frac + back - 1) * stepMs : 1e9;     // ms since this step ended: a quick trail
      s.el.classList.toggle('now', back === 0);
      s.el.classList.toggle('t1', since < 45);
      s.el.classList.toggle('t2', since >= 45 && since < 110);
    });
    [...mini.children].forEach((g, p) => { for (let k = 0; k < 16; ++k) g.children[k].classList.toggle('now', p * 16 + k === cur); });
    for (let i = 0; i < 24; ++i) {
      const vs = [8, 4, 2, 1, 6, 3][E.track(i, FIELD.division)], vl = E.track(i, FIELD.active), c = ppq >= 0 ? Math.floor(ppq * vs) % vl : -1;
      const strip = voices[i].strip.children;
      for (let k = 0; k < 16; ++k) strip[k].classList.toggle('now', k === c);
    }
    // the output level
    if (E.midi.connected) { meterName.textContent = 'MIDI'; meterVal.textContent = ''; }
    else {
      meterName.textContent = 'SORTIE';
      const db = E.outPeak > 0 ? 20 * Math.log10(E.outPeak) : -60, lvl = Math.max(0, Math.min(12, Math.round((db + 48) / 4)));
      meterVal.textContent = E.audioReady ? (db > -60 ? Math.round(db) + ' dB' : '') : 'muet';
      [...meterBar.children].forEach((b, i) => { b.className = i < lvl ? (i >= 10 ? 'hot' : 'on') : ''; });
    }
    if (ppq >= 0 && Math.floor(ppq) !== lastBeat) lastBeat = Math.floor(ppq);
  }
  if (popEl && popEl.style.display === 'block' && now > popUntil) {
    popEl.style.opacity = Math.max(0, 1 - (now - popUntil) / 300);
    if (now - popUntil > 300) popEl.style.display = 'none';
  }
  requestAnimationFrame(frame);
}

//==============================================================================
// MIDI: the real unit
async function midiDialog () {
  const old = stage.querySelector('.dlg'); if (old) { old.remove(); if (old.dataset.kind === 'midi') return; }
  const d = el('div', 'dlg', stage);
  d.dataset.kind = 'midi';
  el('h2', '', d, 'MIDI · LA VRAIE MACHINE');
  const body = el('div', '', d);
  const close = () => d.remove();
  if (!E.midiAvailable()) {
    el('p', '', body, "Ce navigateur ne parle pas MIDI. Le simulateur joue à la place de la machine.");
    el('p', 'small', body, "Sur iPad ou iPhone : ouvre cette page dans une appli qui ajoute le MIDI au navigateur (Web MIDI Browser, par exemple). Sur ordinateur : Chrome ou Edge.");
  } else {
    let access = null;
    try { access = await E.midiAccess(); }
    catch (e) { el('p', 'err', body, "Le navigateur a refusé l'accès MIDI (il faut accepter, avec les SysEx)."); }
    if (access) {
      const { inputs, outputs } = E.ports();
      let saved = {}; try { saved = JSON.parse(localStorage.getItem('ra.midi') || '{}'); } catch (e) {}
      const pick = (name, list, id) => {
        const l = el('label', '', body, name);
        const s = el('select', '', l);
        for (const p of list) { const o = el('option', '', s, p.name); o.value = p.id; if (p.id === id) o.selected = true; }
        if (!list.length) el('option', '', s, 'aucune');
        return s;
      };
      const inS = pick('ENTRÉE', inputs, saved.inId), outS = pick('SORTIE', outputs, saved.outId);
      el('p', 'small', body, "Les deux câbles : la sortie MIDI de la machine vers l'entrée, et l'inverse. Sur la machine : Ctl. send = SysEx, receive = On.");
      const msg = el('p', E.midi.error ? 'err' : 'small', body, E.midi.error || E.midi.status || (E.midi.connected ? 'Connecté.' : ''));
      const row = el('div', 'row', body);
      const go = el('button', 'key on', row, E.midi.connected ? 'Relire le kit' : 'Connecter');
      go.addEventListener('click', async () => {
        msg.className = 'small'; msg.textContent = 'Je cherche le RackAttack…';
        const off = E.on(w => { if (w === 'midi') { msg.className = E.midi.error ? 'err' : 'small'; msg.textContent = E.midi.error || E.midi.status || (E.midi.connected ? 'Connecté : le kit est lu.' : ''); } });
        if (E.midi.connected) await E.readKit(); else await E.connect(inS.value, outS.value);
        off(); render();
      });
      const sim = el('button', 'key', row, 'Revenir au simulateur');
      sim.addEventListener('click', () => { E.useSimulator(); render(); close(); toast('Simulation'); });
    }
  }
  const row2 = el('div', 'row', d);
  const reset = el('button', 'key', row2, 'Remettre le groove de départ');
  reset.addEventListener('click', () => { if (confirm('Effacer ta séquence et revenir au kit et au groove de départ ?')) { E.resetAll(); S.sel.clear(); render(); close(); } });
  const ok = el('button', 'key', row2, 'Fermer');
  ok.addEventListener('click', close);
}

//==============================================================================
// audio: the output, its volume and latency, an input to hear the real unit through a sound card
async function audioDialog () {
  const old = stage.querySelector('.dlg'); if (old) { old.remove(); if (old.dataset.kind === 'audio') return; }
  if (!E.audio) { try { await E.startAudio(); } catch (e) {} }
  const d = el('div', 'dlg', stage);
  d.dataset.kind = 'audio';
  el('h2', '', d, 'AUDIO · ENTRÉE ET SORTIE');
  const body = el('div', '', d);
  const st = E.settings;
  let devs = await E.devices().catch(() => ({ inputs: [], outputs: [] }));
  const unnamed = list => list.length && list.every(x => !x.name || /^(Entrée|Sortie) \d/.test(x.name));

  // the output
  const outRow = el('label', '', body, 'SORTIE');
  if (E.canChooseOutput() && devs.outputs.length) {
    const s = el('select', '', outRow);
    for (const o of [{ id: '', name: 'Celle du système' }, ...devs.outputs.filter(o => o.id !== 'default')]) {
      const op = el('option', '', s, o.name); op.value = o.id; if ((st.sink || '') === o.id) op.selected = true;
    }
    s.addEventListener('change', () => E.setOutput(s.value).then(() => toast('Sortie : ' + s.selectedOptions[0].textContent)).catch(() => toast('Sortie refusée', 'red')));
  } else el('span', 'note', outRow, 'celle du système (sur iPad : le centre de contrôle, ou la carte son branchée)');

  const slider = (name, value, max, fmt, set) => {
    const l = el('label', '', body, name);
    const r = el('input', 'range', l); r.type = 'range'; r.min = 0; r.max = max; r.step = 0.01; r.value = value;
    const v = el('span', 'rv', l, fmt(value));
    r.addEventListener('input', () => { v.textContent = fmt(+r.value); set(+r.value); });
    return r;
  };
  const pct = x => Math.round(x * 100) + ' %';
  slider('VOLUME', st.volume ?? 1, 1.5, pct, v => E.setVolume(v));

  // latency: a new audio context
  const latRow = el('label', '', body, 'LATENCE');
  const lat = el('select', '', latRow);
  for (const [id, name] of [['interactive', 'Courte (pour jouer)'], ['balanced', 'Normale'], ['playback', 'Confortable (moins de craquements)']]) {
    const op = el('option', '', lat, name); op.value = id; if ((st.latency || 'interactive') === id) op.selected = true;
  }
  const latInfo = el('span', 'rv', latRow);
  const showLat = () => { latInfo.textContent = E.audio ? `${E.latencyMs()} ms · ${String(Math.round(E.audio.sampleRate / 100) / 10).replace(".", ",")} kHz` : ''; };
  showLat();
  lat.addEventListener('change', async () => { await E.setLatency(lat.value); showLat(); toast('Audio relancé'); });

  // the input
  el('div', 'sub', body, 'ENTRÉE : ENTENDRE LA VRAIE MACHINE');
  const inRow = el('label', '', body, 'ENTRÉE');
  const ins = el('select', '', inRow);
  const fillInputs = () => {
    ins.innerHTML = '';
    for (const o of [{ id: '', name: 'Celle du système' }, ...devs.inputs.filter(o => o.id !== 'default')]) {
      const op = el('option', '', ins, o.name); op.value = o.id; if ((st.input || '') === o.id) op.selected = true;
    }
  };
  fillInputs();
  if (!navigator.mediaDevices || !navigator.mediaDevices.getUserMedia) ins.disabled = true;
  const allow = el('button', 'key', inRow, 'Autoriser');
  allow.style.display = unnamed(devs.inputs) || !devs.inputs.length ? '' : 'none';
  allow.addEventListener('click', async () => {
    try { await E.askPermission(); devs = await E.devices(); fillInputs(); allow.style.display = 'none'; }
    catch (e) { toast("Accès à l'entrée refusé", 'red'); }
  });
  ins.addEventListener('change', () => E.openInput(ins.value).catch(() => toast("Entrée refusée", 'red')));

  const monRow = el('label', 'check', body);
  const mon = el('input', '', monRow); mon.type = 'checkbox'; mon.checked = !!st.monitor;
  el('span', '', monRow, "Écouter l'entrée dans la sortie");
  mon.addEventListener('change', async () => {
    if (mon.checked && !E.inputStream) { try { await E.openInput(ins.value); } catch (e) { mon.checked = false; toast("Entrée refusée", 'red'); return; } }
    E.setMonitor(mon.checked);
  });
  slider('NIVEAU', st.inputGain ?? 1, 2, pct, v => E.setInputGain(v));
  const vu = el('div', 'vu', body);
  for (let i = 0; i < 24; ++i) el('i', '', vu);
  el('p', 'small', body, "Branche la sortie de la RackAttack sur l'entrée de ta carte son. Avec le micro et les haut-parleurs de la tablette, ça larsène : casque ou carte son.");

  const row = el('div', 'row', d);
  const ok = el('button', 'key', row, 'Fermer');
  const close = () => { cancelAnimationFrame(raf); d.remove(); };
  ok.addEventListener('click', close);
  let raf = 0;
  const meterLoop = () => {
    const p = E.inputPeak(), db = p > 0 ? 20 * Math.log10(p) : -90, lvl = p < 0 ? 0 : Math.max(0, Math.min(24, Math.round((db + 60) / 2.5)));
    [...vu.children].forEach((b, i) => { b.className = i < lvl ? (i >= 21 ? 'hot' : 'on') : ''; });
    raf = requestAnimationFrame(meterLoop);
  };
  meterLoop();
}

//==============================================================================
function showPage (p) {
  S.page = p;
  seqPage.style.display = p === 'seq' ? '' : 'none';
  sonPage.style.display = p === 'son' ? '' : 'none';
  render();
}

// keys: space plays, arrows change the sound
addEventListener('keydown', ev => {
  if (ev.target.closest && ev.target.closest('select, input')) return;
  if (ev.code === 'Space') { ev.preventDefault(); E.toggle().then(render); }
  if (ev.key === 'ArrowRight' && ev.altKey) selectTrack((S.track + 1) % 24);
  if (ev.key === 'ArrowLeft' && ev.altKey) selectTrack((S.track + 23) % 24);
  if ((ev.metaKey || ev.ctrlKey) && ev.key === 'z') { ev.preventDefault(); undo(); render(); }
});

E.on(what => {
  if (what === 'transport') renderHeader();
  if (what === 'midi') renderTop();
  if (what === 'kit') { render(); }
});

// the first screen: a tap starts the sound
const startEl = document.getElementById('start');
const rot = el('div', 'rotate', document.body, 'TOURNE LA TABLETTE : C\'EST FAIT POUR LE PAYSAGE');
(async () => {
  try {
    await document.fonts.load('22px VT'); await document.fonts.load('700 12px Silk');
  } catch (e) {}
  const sg = document.getElementById('start-ghost');
  const spin = () => { if (startEl.classList.contains('gone')) return; const t = performance.now() / 1000; drawGhost(sg, 6, t, Math.round(Math.sin(t * 3) * 3 + 3), (performance.now() % 3000) < 120); requestAnimationFrame(spin); };
  spin();
  await E.load('ra.wasm');
  showPage('seq');
  requestAnimationFrame(frame);
  document.getElementById('start-go').addEventListener('click', async () => {
    startEl.classList.add('gone');
    try { await E.startAudio(); } catch (e) { toast('Pas de son dans ce navigateur', 'red'); }
    render();
  });
})();
