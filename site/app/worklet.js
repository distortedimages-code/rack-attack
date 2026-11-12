// The simulator in the audio thread: ra.wasm's synth, given timed MIDI messages by the page and playing each at
// its sample, as the plug-in's SimDevice does.
class RaSynth extends AudioWorkletProcessor {
  constructor (options) {
    super();
    const noop = () => 0;
    const inst = new WebAssembly.Instance(new WebAssembly.Module(options.processorOptions.wasm),
      { wasi_snapshot_preview1: { fd_write: noop, fd_close: noop, fd_seek: noop } });
    this.x = inst.exports;
    this.x._initialize();
    this.x.ra_synth_init(sampleRate, 128);
    this.buf = this.x.ra_alloc(4096);
    this.pending = [];             // { frame, d } sorted by frame
    this.peak = 0;
    this.count = 0;
    this.port.onmessage = (e) => this.message(e.data);
  }

  bytes (d) { new Uint8Array(this.x.memory.buffer, this.buf, d.length).set(d); return d.length; }

  message (m) {
    switch (m.t) {
      case 'kit':
        m.sounds.forEach((d, i) => { this.bytes(d); this.x.ra_synth_set_sound(i, this.buf); });
        m.fx.forEach((d, i) => { this.bytes(d); this.x.ra_synth_set_fx(i, this.buf); });
        this.x.ra_synth_channel(m.channel || 1);
        if (m.tempo) this.x.ra_synth_tempo(m.tempo);
        break;
      case 'tempo': this.x.ra_synth_tempo(m.bpm); break;
      case 'off': this.pending.length = 0; this.x.ra_synth_all_off(); break;
      case 'stop':
        this.pending.length = 0;                 // what was planned ahead doesn't play
        for (const e of m.list) this.midi(e.d);
        break;
      case 'midi':
        for (const e of m.list) {
          if (e.at < 0) { this.midi(e.d); continue; }
          this.pending.push({ frame: Math.round(e.at * sampleRate), d: e.d });
        }
        this.pending.sort((a, b) => a.frame - b.frame);
        break;
    }
  }

  midi (d) { const n = this.bytes(d); this.x.ra_synth_midi(this.buf, n); }

  process (inputs, outputs) {
    const out = outputs[0], L = out[0], R = out[1] || out[0], n = L.length;
    // late messages (planned for before this block): now
    while (this.pending.length && this.pending[0].frame <= currentFrame) this.midi(this.pending.shift().d);
    let pos = 0;
    while (pos < n) {
      let upto = n;
      if (this.pending.length) { const off = this.pending[0].frame - currentFrame; if (off < n) upto = Math.max(pos, off); }
      if (upto > pos) {
        this.x.ra_synth_render(upto - pos);
        const l = new Float32Array(this.x.memory.buffer, this.x.ra_synth_left(), upto - pos);
        const r = new Float32Array(this.x.memory.buffer, this.x.ra_synth_right(), upto - pos);
        L.set(l, pos); R.set(r, pos);
        for (let i = 0; i < l.length; ++i) { const a = Math.abs(l[i]), b = Math.abs(r[i]); if (a > this.peak) this.peak = a; if (b > this.peak) this.peak = b; }
        pos = upto;
      }
      // the messages due at this sample
      while (this.pending.length && this.pending[0].frame - currentFrame <= pos && pos < n) this.midi(this.pending.shift().d);
    }
    if (++this.count >= 16) { this.port.postMessage({ t: 'peak', v: this.peak }); this.peak = 0; this.count = 0; }
    return true;
  }
}

registerProcessor('ra-synth', RaSynth);
