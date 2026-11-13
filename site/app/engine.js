// The web version's engine: the plug-in's model and player (ra.wasm, on this page), the simulator (the same
// module, in an audio worklet), and Web MIDI for the real unit. Timing as in the plug-in's Sequencer: every
// 20 ms the player plans a little ahead and each message gets its own time.

import { unzip, kitStore } from './waldorf.js';

const LOOKAHEAD = 0.12;          // seconds planned ahead (the plug-in: 40 ms; browsers' timers are rougher)
const LEAD_MS = 4.2;             // a lock goes this far before its note (one SysEx on the cable)
const START_TOLERANCE_MS = 15;
export const PATTERNS_SET = 'Patterns seuls';

export class Engine {
  constructor () {
    this.listeners = new Set();
    this.playing = false;
    this.hits = new Float64Array(24).fill(-1e9);       // when each voice last played (performance.now)
    this.midi = { access: null, out: null, in: null, connected: false, reading: 0, device: 0x7f, channel: 1, error: '' };
    this.sim = true;                                    // the simulator plays (no unit, or chosen)
    this.loadSettings();
    this.audioReady = false;
    this.outPeak = 0;
  }

  on (fn) { this.listeners.add(fn); return () => this.listeners.delete(fn); }
  emit (what) { for (const fn of this.listeners) fn(what); }

  //============================================================================
  async load (url) {
    this.wasmBytes = await (await fetch(url)).arrayBuffer();
    const inst = await WebAssembly.instantiate(this.wasmBytes, { wasi_snapshot_preview1: { fd_write: () => 0, fd_close: () => 0, fd_seek: () => 0 } });
    this.x = inst.instance.exports;
    this.x._initialize();
    this.x.ra_model_init(3);
    this.restore();
  }

  // memory helpers
  scratch (n) { return new Uint8Array(this.x.memory.buffer, this.x.ra_scratch(), n).slice(); }
  text (n) { return new TextDecoder().decode(this.scratch(n)); }
  withBytes (bytes, fn) {
    const p = this.x.ra_alloc(bytes.length);
    new Uint8Array(this.x.memory.buffer, p, bytes.length).set(bytes);
    try { return fn(p, bytes.length); } finally { this.x.ra_free(p); }
  }
  withString (s, fn) { const b = new TextEncoder().encode(s + '\0'); return this.withBytes(b, p => fn(p)); }

  //============================================================================
  // the kit
  kitName () { return this.text(this.x.ra_kit_name()); }
  // what the pages show: the kit heard, or the end of the morph that is armed
  sound (s, a) { return this.x.ra_shown_sound(s, a); }
  fx (e, a) { return this.x.ra_shown_fx(e, a); }
  heard (s, a) { return this.x.ra_sound_get(s, a); }
  paramName (a, object = 0) { return this.text(this.x.ra_param_name(object, a)); }
  valueNames (a, object = 0) {
    const key = object + ':' + a;
    this.namesCache ||= new Map();
    if (!this.namesCache.has(key)) this.namesCache.set(key, this.text(this.x.ra_value_names(object, a)).split('\n').filter(Boolean));
    return this.namesCache.get(key);
  }
  paramMax (a) { const n = this.valueNames(a); return n.length ? n.length - 1 : 127; }

  // an edit: into the armed end of the morph (heard as far as the fader goes there), else at its position
  setSound (s, a, v) {
    v = Math.max(0, Math.min(this.paramMax(a), Math.round(v)));
    if (this.sound(s, a) === v) return;
    const n = this.x.ra_sound_edit(s, a, v);
    if (n > 0) this.send(this.scratch(n));
    this.save();
    this.emit('kit'); this.emit('morph');
  }

  setFx (e, a, v) {
    const n = this.x.ra_fx_edit(e, a, Math.max(0, Math.min(127, Math.round(v))));
    if (n > 0) this.send(this.scratch(n));
    this.save();
    this.emit('kit'); this.emit('morph');
  }

  //============================================================================
  // the morph, as the plug-in's fader: kits A and B, a fader that glides, A or B armed (as Octatrack scenes)
  morphDiffers () { return this.x.ra_morph_differs() === 1; }
  morphPosition () { return this.x.ra_morph_position(); }
  morphArmed () { return this.x.ra_morph_armed(); }                // -1, 0 (A) or 1 (B)
  morphGlide () { return this.settings.glide ?? 150; }
  setMorphGlide (ms) { this.settings.glide = ms; this.saveSettings(); this.emit('morph'); }
  morphArm (side) { this.x.ra_morph_arm(side); this.emit('kit'); this.emit('morph'); }

  // the messages one after the other (as the module gives them) -> the simulator and the unit
  sendAll (bytes) {
    if (!bytes.length) return;
    const list = [];
    for (let i = 0, start = 0; i < bytes.length; ++i)
      if (bytes[i] === 0xf7) { list.push(bytes.slice(start, i + 1)); start = i + 1; }
    if (this.node && this.sim) this.node.port.postMessage({ t: 'midi', list: list.map(d => ({ at: -1, d })) });
    if (this.midi.out && this.midi.connected) for (const d of list) try { this.midi.out.send(d); } catch (e) { this.midiError(e); return; }
  }
  morphResult (n) {
    if (n > 0) this.sendAll(this.scratch(n));
    this.saveMorphSoon();
    this.kitSoon();
    this.emit('morph');
  }
  // the screen follows the knobs as they move, but not 50 times a second
  kitSoon () { if (this.kitTimer) return; this.kitTimer = setTimeout(() => { this.kitTimer = 0; this.emit('kit'); }, 90); }

  // the fader glides to where it is put
  morphTo (p) {
    this.morphTarget = Math.max(0, Math.min(1, p));
    if (this.morphGlide() <= 0) { this.morphNow(this.morphTarget); return; }
    this.emit('morph');
    if (this.glideTimer) return;
    let last = performance.now();
    this.glideTimer = setInterval(() => {
      const now = performance.now(), dt = Math.min(100, now - last);
      last = now;
      if (this.readingKit) return;
      const p0 = this.morphPosition(), t = this.morphTarget;
      let next = p0 + (t - p0) * (1 - Math.exp(-dt * 3 / Math.max(1, this.morphGlide())));
      if (Math.abs(t - next) < 0.003) next = t;
      this.morphNow(next);
      if (next === t) { clearInterval(this.glideTimer); this.glideTimer = 0; }
    }, 20);
  }
  morphNow (p) { this.morphResult(this.x.ra_morph_set_position(p)); }
  morphApply () { this.morphResult(this.x.ra_morph_apply()); }
  morphCopyHeard (side) { this.morphResult(this.x.ra_morph_copy_heard(side)); }
  morphSwap () { this.morphResult(this.x.ra_morph_swap()); }
  morphFlatten () { this.x.ra_morph_flatten(); this.saveMorphSoon(); this.emit('morph'); }
  morphLoadDemo (side, program) { this.morphResult(this.x.ra_morph_load_demo(side, program)); }
  morphLoadKit (side, bytes) {
    const n = this.withBytes(bytes, (p, len) => this.x.ra_morph_load_kit(side, p, len));
    if (n < 0) return false;
    this.morphResult(n);
    return true;
  }
  morphBytes () { return this.scratch(this.x.ra_morph_save()); }
  loadMorph (bytes) {
    const n = this.withBytes(bytes, (p, len) => this.x.ra_morph_restore(p, len));
    if (n < 0) return false;
    this.morphTarget = this.morphPosition();
    this.morphResult(n);
    this.emit('kit');
    return true;
  }
  saveMorphSoon () {
    clearTimeout(this.morphSaveTimer);
    this.morphSaveTimer = setTimeout(() => {
      try {
        if (this.morphDiffers()) localStorage.setItem('ra.morph', b64(this.morphBytes()));
        else localStorage.removeItem('ra.morph');
      } catch (e) {}
      this.save();
    }, 400);
  }

  //============================================================================
  // Waldorf's files (factory sets, bonus kits, patterns): read here, kept in this browser, never sent to the site
  kits () { return kitStore.all(); }
  removeKitSet (set) { return kitStore.removeSet(set).then(() => this.emit('kits')); }

  async importFiles (files) {
    const result = { kits: 0, patterns: 0, skipped: 0, sets: [] }, records = [];
    for (const f of files) {
      const data = new Uint8Array(await f.arrayBuffer());
      const dot = f.name.lastIndexOf('.'), stem = dot > 0 ? f.name.slice(0, dot) : f.name;
      const isZip = dot > 0 && f.name.slice(dot + 1).toLowerCase() === 'zip';
      const entries = isZip ? await unzip(data, stem) : [{ set: '', stem, data }];
      if (!entries.length) ++result.skipped;
      for (const e of entries) this.importData(e, records, result);
    }
    // the same file in two zips (Waldorf's have that) counts once
    const unique = [...new Map(records.map(r => [r.id, r])).values()];
    result.kits = unique.filter(r => r.kit).length;
    result.patterns = unique.length - result.kits;
    if (unique.length) await kitStore.put(unique);
    this.emit('kits');
    return result;
  }
  // as the plug-in's library: a set of programs keeps its file's name, single kits the name of their zip
  importData ({ set, stem, data }, records, result) {
    const n = this.withBytes(data, (p, len) => this.x.ra_bank_read(p, len));
    if (n <= 0) { ++result.skipped; return; }                  // the system updates, and anything that is no kit
    const tidy = t => t.replace(/_/g, ' ').replace(/\s+/g, ' ').trim();
    for (let i = 0; i < n; ++i) {
      const syx = this.scratch(this.x.ra_bank_syx(i)), patterns = this.x.ra_bank_patterns(i);
      if (!this.x.ra_bank_has_kit(i)) {
        const name = tidy(stem) + (n > 1 ? ' ' + (i + 1) : '');
        records.push({ id: PATTERNS_SET + '/' + name, set: PATTERNS_SET, name, number: -1, kit: false, patterns, syx });
        ++result.patterns;
        continue;
      }
      const setName = tidy(n > 1 ? stem : set || 'Kits');
      const stored = this.x.ra_bank_number(i);
      let number = stored >= 0 ? stored + 1 : n > 1 ? i + 1 : 0;
      if (!number && /^prog\d/i.test(stem)) number = parseInt(stem.slice(4), 10) || 0;   // "Prog33_New_Style_Kit"
      const title = this.text(this.x.ra_bank_name(i)).trim() || tidy(stem);
      const name = (number > 0 ? 'P' + String(number).padStart(2, '0') + ' ' : '') + title;
      records.push({ id: setName + '/' + name, set: setName, name, number, kit: true, patterns, syx });
      ++result.kits;
      if (!result.sets.includes(setName)) result.sets.push(setName);
    }
    this.x.ra_bank_clear();
  }

  // a kit (a .syx) -> the kit heard, in the simulator and in the unit's edit buffer when it is connected (nothing is
  // stored on the unit: Shift + Store there keeps it). The morph starts again from it.
  takeKit (syx) {
    const n = this.withBytes(syx, (p, len) => this.x.ra_kit_take(p, len));
    if (n < 0) return false;
    this.sendPaced(this.scratch(n));
    this.morphTarget = this.morphPosition();
    this.sendKitToSynth();
    this.save(); this.saveMorphSoon();
    this.emit('kit'); this.emit('morph');
    return true;
  }
  // many dumps for the unit: one after the other, a little apart (its MIDI buffer is small)
  sendPaced (bytes) {
    if (!this.midi.out || !this.midi.connected || !bytes.length) return;
    let t = performance.now() + 5;
    for (let i = 0, start = 0; i < bytes.length; ++i)
      if (bytes[i] === 0xf7) {
        const d = bytes.slice(start, i + 1);
        start = i + 1;
        try { this.midi.out.send(d, t); } catch (e) { this.midiError(e); return; }
        t += d.length > 40 ? 60 : 4;
      }
  }

  // the internal patterns of the sounds (the unit's Pattern Play)
  kitPatterns () { return this.x.ra_kit_patterns(); }
  kitPatternsRead () { return this.x.ra_kit_patterns_read(); }
  // -> the sequencer's tracks: from a kit kept here (syx), or the kit heard
  patternsToSeq (syx = null, all = true) {
    const n = syx ? this.withBytes(syx, (p, len) => this.x.ra_patterns_to_seq(p, len, all ? 1 : 0)) : this.x.ra_patterns_to_seq(0, 0, all ? 1 : 0);
    if (n > 0) this.edited();
    return { filled: n, approximate: this.x.ra_patterns_approx() };
  }
  // the sequencer's tracks (all, or one) -> the internal patterns of the kit, and of the unit when it is connected
  seqToPatterns (track = -1) { this.sendPaced(this.scratch(this.x.ra_patterns_from_seq(track))); this.save(); }
  // the patterns of a kit kept here -> those of the kit heard (and of the unit)
  takePatterns (syx) {
    const n = this.withBytes(syx, (p, len) => this.x.ra_patterns_take(p, len));
    if (n < 0) return false;
    this.sendPaced(this.scratch(n));
    this.save();
    return true;
  }
  // the unit's own patterns -> the kit (the unit answers each sound's request with its pattern)
  async readPatterns () {
    if (!this.midi.connected) return 0;
    this.readingKit = true;
    let done = 0;
    for (let s = 0; s < 24; ++s) {
      this.midi.status = `Je lis les patterns… ${s}/24`;
      this.emit('midi');
      if (await this.request(this.scratch(this.x.ra_msg_pattern_request(s)), d => d[0] === 0xf0 && d[4] === 0x13, 800)) ++done;
    }
    this.readingKit = false;
    this.midi.status = done === 24 ? '' : `Patterns lus en partie (${done}/24)`;
    this.save();
    this.emit('midi');
    return done;
  }

  //============================================================================
  // the pattern
  tempo () { return this.x.ra_tempo(); }
  setTempo (t) { this.anchor(); this.x.ra_set_tempo(t); this.edited(); }
  swing () { return this.x.ra_swing(); }
  setSwing (s) { this.x.ra_set_swing(s); this.edited(); }
  track (t, f) { return this.x.ra_track_get(t, f); }
  setTrack (t, f, v) { this.x.ra_track_set(t, f, v); this.edited(); }
  trackName (t) { const n = this.text(this.x.ra_track_name(t)); return n || 'SON ' + String(t + 1).padStart(2, '0'); }
  setTrackName (t, n) { this.withString(n, p => this.x.ra_track_set_name(t, p)); this.edited(); }
  isOn (t, s) { return this.x.ra_track_is_on(t, s) === 1; }
  step (t, s, f) { return this.x.ra_step_get(t, s, f); }
  setStep (t, s, f, v) { this.x.ra_step_set(t, s, f, v); this.edited(); }
  lock (t, s, a) { return this.x.ra_lock_get(t, s, a); }
  locksAt (t, s) { const n = this.step(t, s, 9), out = []; for (let i = 0; i < n; ++i) out.push(this.x.ra_lock_address(t, s, i)); return out; }
  setLock (t, s, a, v) { const ok = this.x.ra_lock_set(t, s, a, v) === 1; this.edited(); return ok; }
  clearLock (t, s, a) { this.x.ra_lock_clear(t, s, a); this.edited(); }
  trackOp (t, op, arg = 0) { this.x.ra_track_op(t, op, arg); this.edited(); }

  patternBytes () { return this.scratch(this.x.ra_pattern_save()); }
  loadPattern (bytes) { const ok = this.withBytes(bytes, (p, n) => this.x.ra_pattern_load(p, n)) === 1; if (ok) this.edited(); return ok; }

  edited () { this.save(); this.emit('pattern'); }

  // the browser keeps the pattern and the kit between visits
  save () {
    clearTimeout(this.saveTimer);
    this.saveTimer = setTimeout(() => {
      try {
        localStorage.setItem('ra.pattern', b64(this.patternBytes()));
        localStorage.setItem('ra.kit', b64(this.scratch(this.x.ra_kit_save())));
      } catch (e) { /* private mode: nothing kept */ }
    }, 300);
  }
  restore () {
    try {
      const p = localStorage.getItem('ra.pattern'), k = localStorage.getItem('ra.kit');
      if (k) this.withBytes(unb64(k), (ptr, n) => this.x.ra_kit_load(ptr, n));
      if (p) this.withBytes(unb64(p), (ptr, n) => this.x.ra_pattern_load(ptr, n));
      const m = localStorage.getItem('ra.morph');
      if (m) this.withBytes(unb64(m), (ptr, n) => this.x.ra_morph_restore(ptr, n));    // the kit becomes its blend
      this.morphTarget = this.x.ra_morph_position();
    } catch (e) { /* nothing kept */ }
  }
  resetAll () {
    try { localStorage.removeItem('ra.pattern'); localStorage.removeItem('ra.kit'); localStorage.removeItem('ra.morph'); } catch (e) {}
    this.x.ra_model_init(3);
    this.morphTarget = 0;
    this.sendKitToSynth();
    this.emit('kit'); this.emit('pattern');
  }

  //============================================================================
  // sound: the simulator in an audio worklet (started by a tap: browsers want one)
  async startAudio () {
    if (this.audio) { if (this.audio.state !== 'running') await this.audio.resume(); return; }
    const AC = window.AudioContext || window.webkitAudioContext;
    // iPhone / iPad: play even with the silent switch on (it is an instrument)
    try { if (navigator.audioSession) navigator.audioSession.type = this.settings.monitor ? 'play-and-record' : 'playback'; } catch (e) {}
    const opts = { latencyHint: this.settings.latency || 'interactive' };
    if (this.settings.sink && 'setSinkId' in AC.prototype) opts.sinkId = this.settings.sink;
    try { this.audio = new AC(opts); } catch (e) { delete opts.sinkId; this.audio = new AC(opts); }
    const resumed = this.audio.resume().catch(() => {});      // still inside the tap that started it
    await this.audio.audioWorklet.addModule('worklet.js');
    this.node = new AudioWorkletNode(this.audio, 'ra-synth', { numberOfInputs: 0, outputChannelCount: [2], processorOptions: { wasm: this.wasmBytes } });
    // the simulator and the monitored input meet in the master volume
    this.master = this.audio.createGain();
    this.master.gain.value = this.settings.volume ?? 1;
    this.node.connect(this.master);
    this.master.connect(this.audio.destination);
    this.node.port.onmessage = (e) => { if (e.data.t === 'peak') this.outPeak = e.data.v; };
    this.sendKitToSynth();
    await resumed;
    if (this.audio.state !== 'running') await this.audio.resume().catch(() => {});
    this.audioReady = true;
    if (this.settings.monitor || this.settings.input) this.openInput(this.settings.input).catch(() => {});
    this.emit('audio');
  }

  //============================================================================
  // audio settings: output device, volume, latency, the input (to hear the real unit through an interface)
  loadSettings () {
    try { this.settings = JSON.parse(localStorage.getItem('ra.audio') || '{}'); } catch (e) { this.settings = {}; }
  }
  saveSettings () { try { localStorage.setItem('ra.audio', JSON.stringify(this.settings)); } catch (e) {} }

  canChooseOutput () { const AC = window.AudioContext || window.webkitAudioContext; return !!AC && 'setSinkId' in AC.prototype; }

  async devices () {
    if (!navigator.mediaDevices || !navigator.mediaDevices.enumerateDevices) return { inputs: [], outputs: [] };
    const all = await navigator.mediaDevices.enumerateDevices();
    const named = (d, i, kind) => ({ id: d.deviceId, name: d.label || `${kind} ${i + 1}` });
    return {
      inputs: all.filter(d => d.kind === 'audioinput').map((d, i) => named(d, i, 'Entrée')),
      outputs: all.filter(d => d.kind === 'audiooutput').map((d, i) => named(d, i, 'Sortie')),
    };
  }

  // the browser shows device names once it may use an input
  async askPermission () {
    const s = await navigator.mediaDevices.getUserMedia({ audio: true });
    s.getTracks().forEach(t => t.stop());
  }

  async setOutput (id) {
    this.settings.sink = id; this.saveSettings();
    if (this.audio && this.audio.setSinkId) await this.audio.setSinkId(id || '');
  }

  setVolume (v) {
    this.settings.volume = v; this.saveSettings();
    if (this.master) this.master.gain.setTargetAtTime(v, this.audio.currentTime, 0.02);
  }

  // a new latency needs a new audio context: the simulator restarts with the same kit
  async setLatency (hint) {
    this.settings.latency = hint; this.saveSettings();
    if (!this.audio) return;
    const was = this.playing;
    if (was) this.stop();
    this.closeInput();
    try { await this.audio.close(); } catch (e) {}
    this.audio = null; this.node = null; this.master = null; this.audioReady = false;
    await this.startAudio();
    if (was) await this.play();
  }

  latencyMs () {
    if (!this.audio) return 0;
    return Math.round(((this.audio.baseLatency || 0) + (this.audio.outputLatency || 0)) * 1000);
  }

  async openInput (id) {
    if (!this.audio) return;
    this.closeInput();
    this.settings.input = id || ''; this.saveSettings();
    const audio = { echoCancellation: false, noiseSuppression: false, autoGainControl: false, channelCount: { ideal: 2 } };
    if (id) audio.deviceId = { exact: id };
    this.inputStream = await navigator.mediaDevices.getUserMedia({ audio });
    this.inputNode = this.audio.createMediaStreamSource(this.inputStream);
    this.inputGain = this.audio.createGain();
    this.inputGain.gain.value = this.settings.monitor ? (this.settings.inputGain ?? 1) : 0;
    this.inputMeter = this.audio.createAnalyser();
    this.inputMeter.fftSize = 512;
    this.inputNode.connect(this.inputMeter);
    this.inputNode.connect(this.inputGain);
    this.inputGain.connect(this.master);
    this.emit('audio');
  }

  closeInput () {
    if (this.inputStream) this.inputStream.getTracks().forEach(t => t.stop());
    try { this.inputNode && this.inputNode.disconnect(); this.inputGain && this.inputGain.disconnect(); } catch (e) {}
    this.inputStream = this.inputNode = this.inputGain = this.inputMeter = null;
  }

  setMonitor (on) {
    this.settings.monitor = on; this.saveSettings();
    try { if (navigator.audioSession) navigator.audioSession.type = on ? 'play-and-record' : 'playback'; } catch (e) {}
    if (this.inputGain) this.inputGain.gain.setTargetAtTime(on ? (this.settings.inputGain ?? 1) : 0, this.audio.currentTime, 0.02);
  }

  setInputGain (v) {
    this.settings.inputGain = v; this.saveSettings();
    if (this.inputGain && this.settings.monitor) this.inputGain.gain.setTargetAtTime(v, this.audio.currentTime, 0.02);
  }

  inputPeak () {
    if (!this.inputMeter) return -1;
    const buf = this.peakBuf || (this.peakBuf = new Float32Array(512));
    this.inputMeter.getFloatTimeDomainData(buf);
    let p = 0; for (const v of buf) p = Math.max(p, Math.abs(v));
    return p;
  }

  sendKitToSynth () {
    if (!this.node) return;
    const sounds = [], fx = [];
    for (let i = 0; i < 24; ++i) sounds.push(this.scratch(this.x.ra_sound_data(i)));
    for (let e = 0; e < 4; ++e) fx.push(this.scratch(this.x.ra_fx_data(e)));
    this.node.port.postMessage({ t: 'kit', sounds, fx, channel: this.midi.channel, tempo: this.tempo() });
  }

  // a message for the unit and the simulator, now
  send (msg) {
    if (this.node && this.sim) this.node.port.postMessage({ t: 'midi', list: [{ at: -1, d: msg }] });
    if (this.midi.out && this.midi.connected) try { this.midi.out.send(msg); } catch (e) { this.midiError(e); }
  }

  // a voice tapped: its note, on and off
  audition (s, note = 60, velocity = 100) {
    const k = this.x.ra_note_for(s, note), ch = k >> 8, n = k & 255;
    this.send(new Uint8Array([0x90 | ch, n, velocity]));
    setTimeout(() => this.send(new Uint8Array([0x80 | ch, n, 0])), 180);
    this.hits[s] = performance.now();
  }

  //============================================================================
  // the clock: audio time when there is sound, else the page's clock
  now () { return this.audio && this.audioReady ? this.audio.currentTime : performance.now() / 1000; }

  anchor () {
    // a tempo change while playing: the beat continues from where it is
    if (!this.playing) return;
    const t = this.now();
    this.anchorPpq = this.ppqAt(t);
    this.anchorTime = t;
  }
  ppqAt (t) { return this.anchorPpq + (t - this.anchorTime) * this.tempo() / 60; }
  timeOf (ppq) { return this.anchorTime + (ppq - this.anchorPpq) * 60 / this.tempo(); }

  // the playhead for the screen, -1 stopped
  playhead () {
    if (!this.playing) return -1;
    const lat = this.audio && this.sim ? (this.audio.outputLatency || this.audio.baseLatency || 0) : 0;
    return Math.max(0, this.ppqAt(this.now() - lat));
  }

  async play () {
    if (this.playing) return;
    if (this.sim) await this.startAudio().catch(() => {});
    this.x.ra_play_reset();
    this.anchorTime = this.now() + 0.05;
    this.anchorPpq = 0;
    this.planned = this.anchorPpq - START_TOLERANCE_MS / 1000 * this.tempo() / 60;
    this.playing = true;
    this.lastTime = this.anchorTime;
    this.tick();
    this.timer = setInterval(() => this.tick(), 20);
    this.emit('transport');
  }

  stop () {
    if (!this.playing) return;
    clearInterval(this.timer);
    this.playing = false;
    const at = this.ppqAt(this.now());
    const ev = this.parse(this.x.ra_stop(at));
    if (this.node) this.node.port.postMessage({ t: 'stop', list: ev.map(e => ({ at: -1, d: e.d })) });
    // the unit: after what was already sent ahead
    const when = Math.max(performance.now(), this.lastMidiStamp || 0) + 1;
    if (this.midi.out && this.midi.connected) for (const e of ev) try { this.midi.out.send(e.d, when); } catch (err) { this.midiError(err); }
    this.emit('transport');
  }

  toggle () { return this.playing ? this.stop() : this.play(); }

  tick () {
    if (!this.playing) return;
    const until = this.ppqAt(this.now() + LOOKAHEAD);
    const lead = LEAD_MS / 1000 * this.tempo() / 60;
    const ev = this.parse(this.x.ra_plan(this.planned, until, lead));
    this.planned = until;
    if (!ev.length) return;
    const list = [];
    const audioNow = this.now(), pageNow = performance.now();
    for (const e of ev) {
      const at = Math.max(audioNow, this.timeOf(e.ppq));
      list.push({ at: this.sim && this.audioReady ? at : -1, d: e.d });
      const stamp = pageNow + (at - audioNow) * 1000;
      if (this.midi.out && this.midi.connected) {
        try { this.midi.out.send(e.d, stamp); } catch (err) { this.midiError(err); }
        this.lastMidiStamp = Math.max(this.lastMidiStamp || 0, stamp);
      }
      if (e.kind === 1) {
        const lat = this.audio && this.sim ? (this.audio.outputLatency || this.audio.baseLatency || 0) * 1000 : 0;
        const track = e.track;
        setTimeout(() => { this.hits[track] = performance.now(); }, Math.max(0, stamp - pageNow + lat));
      }
    }
    if (this.node && this.sim) this.node.port.postMessage({ t: 'midi', list });
  }

  // timed records from ra_plan / ra_stop
  parse (n) {
    const bytes = this.scratch(this.x.ra_scratch_size());
    const dv = new DataView(bytes.buffer);
    const out = [];
    let off = 0;
    for (let i = 0; i < n; ++i) {
      off = (off + 7) & ~7;
      const ppq = dv.getFloat64(off, true), track = dv.getInt32(off + 8, true), kind = dv.getInt32(off + 12, true), size = dv.getInt32(off + 16, true);
      out.push({ ppq, track, kind, d: bytes.slice(off + 20, off + 20 + size) });
      off += 20 + size;
    }
    return out;
  }

  //============================================================================
  // the real unit, through Web MIDI (Chrome, Edge, or an iPad app that adds it, such as Web MIDI Browser)
  midiAvailable () { return !!navigator.requestMIDIAccess; }

  async midiAccess () {
    if (this.midi.access) return this.midi.access;
    this.midi.access = await navigator.requestMIDIAccess({ sysex: true });
    this.midi.access.onstatechange = () => this.emit('midi');
    return this.midi.access;
  }

  ports () {
    const a = this.midi.access;
    if (!a) return { inputs: [], outputs: [] };
    return { inputs: [...a.inputs.values()], outputs: [...a.outputs.values()] };
  }

  midiError (e) { this.midi.error = String(e && e.message || e); this.emit('midi'); }

  // ports chosen: find the unit (its SysEx ID and channel), then read its kit
  async connect (inId, outId) {
    const a = await this.midiAccess();
    this.disconnect();
    this.midi.in = a.inputs.get(inId) || null;
    this.midi.out = a.outputs.get(outId) || null;
    this.midi.error = '';
    if (!this.midi.in || !this.midi.out) { this.midi.error = 'Choisis une entrée et une sortie.'; this.emit('midi'); return false; }
    try { localStorage.setItem('ra.midi', JSON.stringify({ inId, outId })); } catch (e) {}
    this.midi.in.onmidimessage = (e) => this.incoming(e.data);
    this.midi.status = 'Je cherche le RackAttack…';
    this.emit('midi');
    const global = await this.request(this.scratch(this.x.ra_msg_global_request()), d => this.withBytes(d, (p, n) => this.x.ra_parse_global(p, n)) >= 0, 1500);
    if (!global) { this.midi.error = 'Pas de réponse : vérifie les deux câbles, et Ctl. send = SysEx sur la machine.'; this.midi.status = ''; this.emit('midi'); return false; }
    const g = this.withBytes(global, (p, n) => this.x.ra_parse_global(p, n));
    this.midi.device = g >> 8; this.midi.channel = g & 255;
    this.x.ra_set_device(this.midi.device, this.midi.channel);
    this.midi.connected = true;
    this.sim = false;
    if (this.node) this.node.port.postMessage({ t: 'off' });
    await this.readKit();
    return true;
  }

  async readKit () {
    this.readingKit = true;
    const asks = [this.scratch(this.x.ra_msg_program_request())];
    for (let s = 0; s < 24; ++s) asks.push(this.scratch(this.x.ra_msg_sound_request(s)));
    for (let e = 0; e < 4; ++e) asks.push(this.scratch(this.x.ra_msg_effect_request(e)));
    let done = 0;
    for (const ask of asks) {
      this.midi.status = `Je lis le kit… ${done}/${asks.length}`;
      this.emit('midi');
      const got = await this.request(ask, d => d[0] === 0xf0 && d.length > 20, 800);
      if (got) ++done;
    }
    this.midi.status = done === asks.length ? '' : `Kit lu en partie (${done}/${asks.length})`;
    this.readingKit = false;
    if (this.morphDiffers()) this.morphApply();        // the unit gets what the fader makes of the kit read
    this.sendKitToSynth();
    this.emit('kit'); this.emit('midi'); this.emit('morph');
  }

  // sends a request, waits for the first answer that `accept` takes
  request (msg, accept, timeout) {
    return new Promise((resolve) => {
      const t = setTimeout(() => { this.waiting = null; resolve(null); }, timeout);
      this.waiting = (d) => { if (accept(d)) { clearTimeout(t); this.waiting = null; resolve(d); return true; } return false; };
      try { this.midi.out.send(msg); } catch (e) { clearTimeout(t); this.waiting = null; this.midiError(e); resolve(null); }
    });
  }

  incoming (d) {
    if (d[0] === 0xf0) {
      if (this.waiting) this.waiting(d);
      // dumps and parameter changes from the unit (its knobs, when Ctl. send = SysEx): the screen follows
      const what = this.withBytes(d, (p, n) => this.x.ra_kit_apply(p, n));
      if (what) {
        // with an end armed, the unit's knob went into it: the unit goes back to what the fader makes of it
        if (this.morphArmed() >= 0 && !this.readingKit) this.morphApply();
        this.emit('kit'); this.emit('morph');
      }
      return;
    }
    if ((d[0] & 0xf0) === 0x90 && d[2] > 0) {
      for (let s = 0; s < 24; ++s) {
        const k = this.x.ra_note_for(s, d[1]);
        if ((k >> 8) === (d[0] & 15) && (k & 255) === d[1]) this.hits[s] = performance.now();
      }
    }
  }

  disconnect () {
    if (this.midi.in) this.midi.in.onmidimessage = null;
    this.midi.connected = false;
    this.midi.in = this.midi.out = null;
    this.sim = true;
    this.x.ra_set_device(0x7f, 1);
    this.emit('midi');
  }

  // back to the simulator's own kit
  useSimulator () {
    this.disconnect();
    this.midi.channel = 1;
    this.sendKitToSynth();
  }
}

function b64 (bytes) { let s = ''; for (const b of bytes) s += String.fromCharCode(b); return btoa(s); }
function unb64 (t) { const s = atob(t), b = new Uint8Array(s.length); for (let i = 0; i < s.length; ++i) b[i] = s.charCodeAt(i); return b; }
