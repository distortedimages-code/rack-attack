// A check of the web version's engine (node tools/web/test_wasm.mjs [ra.wasm]): the demo kit and groove load,
// the player plans notes and locks, and the simulator plays them (not silence).
import { readFileSync } from 'node:fs';

const file = process.argv[2] || 'site/app/ra.wasm';
const bytes = readFileSync(file);
let failures = 0;
const check = (ok, what) => { if (!ok) { ++failures; console.log('FAILED: ' + what); } };

function load () {
  let memory;
  const wasi = {
    fd_write: () => 0, fd_close: () => 0, fd_seek: () => 0,
  };
  const inst = new WebAssembly.Instance(new WebAssembly.Module(bytes), { wasi_snapshot_preview1: wasi });
  memory = inst.exports.memory;
  inst.exports._initialize();
  const x = { ...inst.exports };
  x.bytes = (n) => new Uint8Array(memory.buffer, x.ra_scratch(), n).slice();
  x.text = (n) => new TextDecoder().decode(x.bytes(n));
  x.mem = () => memory;
  return x;
}

const m = load();
m.ra_model_init(3);
check(m.text(m.ra_kit_name()) === 'SIM 04', 'kit name: ' + m.text(m.ra_kit_name()));
check(m.text(m.ra_track_name(0)) === 'Kick', 'track 1 name');
check(m.ra_track_get(2, 1) === 64, 'snare length 64');
check(Math.abs(m.ra_tempo() - 124) < 1e-9, 'tempo 124');
check(m.text(m.ra_param_name(0, 0x27)) === 'Cutoff', 'parameter name: ' + m.text(m.ra_param_name(0, 0x27)));
check(m.text(m.ra_value_names(0, 0x01)).split('\n')[0].length > 0, 'shape names');

// what the player plans over two bars
const n = m.ra_plan(0, 8, 0.02);
const plan = m.bytes(m.ra_scratch_size());
const dv = new DataView(plan.buffer);
let off = 0, ons = 0, params = 0;
const events = [];
for (let i = 0; i < n; ++i) {
  off = (off + 7) & ~7;
  const ppq = dv.getFloat64(off, true), track = dv.getInt32(off + 8, true), kind = dv.getInt32(off + 12, true), size = dv.getInt32(off + 16, true);
  const msg = plan.slice(off + 20, off + 20 + size);
  off += 20 + size;
  events.push({ ppq, track, kind, msg });
  if (kind === 1) ++ons;
  if (kind === 0) ++params;
}
check(ons > 20, 'notes planned: ' + ons);
check(params >= 2, 'locks planned: ' + params);
check(events.every(e => e.ppq >= -0.1 && e.ppq < 8.1), 'times in range');
check(events.filter(e => e.kind === 1 && e.track === 0).length === 8, 'eight kicks in two bars');

// the simulator plays them
const s = load();
const rate = 48000, block = 128;
s.ra_synth_init(rate, block);
const ptr = s.ra_alloc(512);
for (let i = 0; i < 24; ++i) {
  const d = m.bytes(m.ra_sound_data(i));
  new Uint8Array(s.mem().buffer, ptr, d.length).set(d);
  s.ra_synth_set_sound(i, ptr);
}
for (let e = 0; e < 4; ++e) {
  const d = m.bytes(m.ra_fx_data(e));
  new Uint8Array(s.mem().buffer, ptr, d.length).set(d);
  s.ra_synth_set_fx(e, ptr);
}
s.ra_synth_channel(1);
s.ra_synth_tempo(124);
const perPpq = rate * 60 / 124;
let frame = 0, peak = 0, sum = 0, count = 0, next = 0;
events.sort((a, b) => a.ppq - b.ppq);
while (frame < perPpq * 8) {
  while (next < events.length && events[next].ppq * perPpq < frame + block) {
    const msg = events[next++].msg;
    new Uint8Array(s.mem().buffer, ptr, msg.length).set(msg);
    s.ra_synth_midi(ptr, msg.length);
  }
  s.ra_synth_render(block);
  const l = new Float32Array(s.mem().buffer, s.ra_synth_left(), block);
  for (const v of l) { peak = Math.max(peak, Math.abs(v)); sum += v * v; ++count; }
  frame += block;
}
const rms = Math.sqrt(sum / count);

// the morph: a demo kit in B, the fader half way, an edit with A armed
{
  const split = (n) => { const b = m.bytes(n), out = []; for (let i = 0, st = 0; i < b.length; ++i) if (b[i] === 0xf7) { out.push(b.slice(st, i + 1)); st = i + 1; } return out; };
  check(m.ra_morph_differs() === 0, 'no morph at first');
  const a27 = m.ra_sound_get(2, 0x27);
  check(split(m.ra_morph_load_demo(1, 9)).length === 0, 'B loaded at A: nothing heard changes');
  const b27 = m.ra_morph_end(1, 2, 0x27);
  check(m.ra_morph_differs() === 1, 'A and B differ');
  const msgs = split(m.ra_morph_set_position(1));
  check(msgs.length > 0 && msgs.every(d => d[0] === 0xf0), 'messages at B: ' + msgs.length);
  check(m.ra_sound_get(2, 0x27) === b27, 'heard = B');
  m.ra_morph_set_position(0.5);
  check(m.ra_sound_get(2, 0x27) === Math.round((a27 + b27) / 2) || Math.abs(m.ra_sound_get(2, 0x27) - (a27 + b27) / 2) <= 0.5, 'half way');
  m.ra_morph_arm(0);
  const out = split(m.ra_sound_edit(2, 0x27, 0));
  check(m.ra_morph_end(0, 2, 0x27) === 0 && m.ra_morph_end(1, 2, 0x27) === b27, 'armed A: only A');
  check(m.ra_shown_sound(2, 0x27) === 0, 'the page shows A');
  check(m.ra_sound_get(2, 0x27) === Math.round(b27 / 2) && out.length === 1, 'heard half of it');
  m.ra_morph_arm(-1);
  const saved = m.bytes(m.ra_morph_save());
  m.ra_morph_flatten();
  check(m.ra_morph_differs() === 0, 'flattened');
  const sp = m.ra_alloc(saved.length);
  new Uint8Array(m.mem().buffer, sp, saved.length).set(saved);
  check(m.ra_morph_restore(sp, saved.length) >= 0 && m.ra_morph_differs() === 1, 'morph back');
}
check(peak > 0.05 && peak < 4, 'peak ' + peak.toFixed(3));
check(rms > 0.005, 'rms ' + rms.toFixed(4));
console.log(`plan: ${n} events (${ons} notes, ${params} parameters) · audio peak ${peak.toFixed(3)} rms ${rms.toFixed(4)}`);
if (failures) { console.log(failures + ' failed'); process.exit(1); }
console.log('web engine: all passed');
