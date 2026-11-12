// The sound's settings as the plug-in shows them: sections (SoundSections.h), the Son page's columns (Pages.cpp),
// how a value reads (ParamText.h), and the quick access row (SeqPage::quickFor).

export const SECTIONS = [
  { title: 'Oscillateur 1', tab: 'Osc 1', rows: [
    [[0x01, 'Shape', 1], [0x02, 'Pitch'], [0x03, 'Detune'], [0x04, 'Start', 1], [0x09, 'FM']],
    [[0x05, 'Pitch Mod'], [0x07, 'Mod Src', 1], [0x06, 'Pitch Ctrl'], [0x08, 'Ctrl Src', 1]],
    [[0x0A, 'FM Mod'], [0x0C, 'Mod Src', 1], [0x0B, 'FM Ctrl'], [0x0D, 'Ctrl Src', 1]]] },
  { title: 'Oscillateur 2', tab: 'Osc 2', rows: [
    [[0x10, 'Shape', 1], [0x11, 'Pitch'], [0x12, 'Detune'], [0x13, 'Start', 1]],
    [[0x14, 'Pitch Mod'], [0x16, 'Mod Src', 1], [0x15, 'Pitch Ctrl'], [0x17, 'Ctrl Src', 1]]] },
  { title: 'Mix / Crack', tab: 'Mix', rows: [
    [[0x1A, 'Osc 1'], [0x1B, 'Osc 2'], [0x1C, 'Ringmod'], [0x1D, 'Crack'], [0x24, 'Crack Speed'], [0x25, 'Crack Length']],
    [[0x1E, 'Osc 2 Mod'], [0x20, 'Mod Src', 1], [0x1F, 'Osc 2 Ctrl'], [0x21, 'Ctrl Src', 1]]] },
  { title: 'Filtre', tab: 'Filtre', rows: [
    [[0x26, 'Type', 1], [0x27, 'Cutoff'], [0x28, 'Resonance'], [0x29, 'Drive'], [0x2A, 'Keytrack']],
    [[0x2B, 'Mod 1'], [0x2D, 'Mod Src', 1], [0x2C, 'Ctrl 1'], [0x2E, 'Ctrl Src', 1]],
    [[0x2F, 'Mod 2'], [0x31, 'Mod Src', 1], [0x30, 'Ctrl 2'], [0x32, 'Ctrl Src', 1]]] },
  { title: 'Ampli', tab: 'Ampli', rows: [
    [[0x35, 'Volume'], [0x3A, 'Pan'], [0x6C, 'Pattern', 1]],
    [[0x36, 'Vol Mod'], [0x38, 'Mod Src', 1], [0x37, 'Vol Ctrl'], [0x39, 'Ctrl Src', 1]],
    [[0x3B, 'Pan Mod'], [0x3D, 'Mod Src', 1], [0x3C, 'Pan Ctrl'], [0x3E, 'Ctrl Src', 1]]] },
  { title: 'Enveloppe 1', tab: 'Env 1', rows: [[[0x3F, 'Attack'], [0x40, 'Decay'], [0x41, 'Sustain'], [0x42, 'Release'], [0x43, 'Shape']]] },
  { title: 'Enveloppe 2', tab: 'Env 2', rows: [[[0x45, 'Attack'], [0x46, 'Decay'], [0x47, 'Sustain'], [0x48, 'Release'], [0x49, 'Shape']]] },
  { title: 'LFO 1', tab: 'LFO 1', rows: [[[0x4B, 'Speed'], [0x4C, 'Shape', 1], [0x4D, 'Sync', 1], [0x4E, 'One Shot', 1]]] },
  { title: 'LFO 2', tab: 'LFO 2', rows: [[[0x4F, 'Speed'], [0x50, 'Shape', 1], [0x51, 'Sync', 1], [0x52, 'Mod'], [0x53, 'Mod Src']]] },
  { title: 'Sortie', tab: 'Sortie', rows: [
    [[0x55, 'Dry Out', 1], [0x56, 'FX', 1], [0x57, 'FX Send'], [0x5E, 'XOR', 1]],
    [[0x58, 'Tuned Chn', 1], [0x59, 'Transpose'], [0x5A, 'Low Key'], [0x5B, 'High Key']]] },
].map(s => ({ ...s, rows: s.rows.map(r => r.map(([a, label, choice]) => ({ a, label, choice: !!choice }))) }));

// the Son page: four columns of cards; a modulation knob has its source under it
export const COLUMNS = [
  [{ title: 'Oscillateur 1', rows: [[[0x01, 'SHAPE'], [0x02, 'PITCH'], [0x03, 'DETUNE'], [0x04, 'START'], [0x09, 'FM']],
                                     [[0x05, 'P.MOD', 0x07], [0x06, 'P.CTRL', 0x08], [0x0A, 'FM MOD', 0x0C], [0x0B, 'FM CTL', 0x0D]]] },
   { title: 'Filtre', rows: [[[0x26, 'TYPE'], [0x27, 'CUTOFF'], [0x28, 'RESO'], [0x29, 'DRIVE'], [0x2A, 'KEYTRK']],
                             [[0x2B, 'MOD 1', 0x2D], [0x2C, 'CTL 1', 0x2E], [0x2F, 'MOD 2', 0x31], [0x30, 'CTL 2', 0x32]]] }],
  [{ title: 'Oscillateur 2', rows: [[[0x10, 'SHAPE'], [0x11, 'PITCH'], [0x12, 'DETUNE'], [0x13, 'START']],
                                     [[0x14, 'P.MOD', 0x16], [0x15, 'P.CTRL', 0x17]]] },
   { title: 'Mix / Crack', rows: [[[0x1A, 'OSC 1'], [0x1B, 'OSC 2'], [0x1C, 'RING'], [0x1D, 'CRACK']],
                                  [[0x24, 'C.SPEED'], [0x25, 'C.LEN'], [0x1E, 'O2 MOD', 0x20], [0x1F, 'O2 CTL', 0x21]]] }],
  [{ title: 'Ampli', rows: [[[0x35, 'VOLUME'], [0x3A, 'PAN'], [0x6C, 'PATTERN']],
                            [[0x36, 'V.MOD', 0x38], [0x37, 'V.CTRL', 0x39], [0x3B, 'PAN MOD', 0x3D], [0x3C, 'PAN CTL', 0x3E]]] },
   { title: 'Sortie', rows: [[[0x55, 'OUT'], [0x56, 'FX'], [0x57, 'SEND'], [0x5E, 'XOR']],
                             [[0x58, 'TUNED'], [0x59, 'TRANSP'], [0x5A, 'LO KEY'], [0x5B, 'HI KEY']]] }],
  [{ title: 'Enveloppe 1', rows: [[[0x3F, 'ATTACK'], [0x40, 'DECAY'], [0x41, 'SUSTAIN'], [0x42, 'RELEASE'], [0x43, 'SHAPE']]] },
   { title: 'Enveloppe 2', rows: [[[0x45, 'ATTACK'], [0x46, 'DECAY'], [0x47, 'SUSTAIN'], [0x48, 'RELEASE'], [0x49, 'SHAPE']]] },
   { title: 'LFO 1', rows: [[[0x4B, 'SPEED'], [0x4C, 'SHAPE'], [0x4D, 'SYNC'], [0x4E, '1-SHOT']]] },
   { title: 'LFO 2', rows: [[[0x4F, 'SPEED'], [0x50, 'SHAPE'], [0x51, 'SYNC'], [0x52, 'MOD'], [0x53, 'MOD SRC']]] }],
].map(col => col.map(card => ({ ...card, rows: card.rows.map(r => r.map(([a, label, src]) => ({ a, label, src: src ?? -1 }))) })));

const BIPOLAR = new Set([0x05, 0x06, 0x0A, 0x0B, 0x14, 0x15, 0x1E, 0x1F, 0x2A, 0x2B, 0x2C, 0x2F, 0x30, 0x36, 0x37, 0x3B, 0x3C, 0x52, 0x3A, 0x59]);
export const isBipolar = a => BIPOLAR.has(a);

const NOTE = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'];
export const noteName = n => NOTE[((n % 12) + 12) % 12] + (Math.floor(n / 12) - 2);   // middle C (60) = C3, as in the plug-in

const sign = x => (x > 0 ? '+' + x : '' + x);

// names: the value names of a list (from the protocol notes), or empty
export function valueText (a, v, names) {
  if (names && names.length) return v >= 0 && v < names.length ? names[v] : '' + v;
  switch (a) {
    case 0x3A: return v === 64 ? 'C' : v < 64 ? 'L' + (64 - v) : 'R' + (v - 64);
    case 0x2A: return sign(Math.round((v - 64) * 3.125)) + '%';
    case 0x59: return sign(Math.max(-48, Math.min(48, v - 64)));
    case 0x5A: case 0x5B: return noteName(v);
    case 0x25: return v >= 50 ? 'INF' : '' + Math.max(1, v);
    case 0x09: case 0x1A: case 0x1B: case 0x1C: case 0x1D: case 0x57: return Math.round(v * 100 / 127) + '%';
  }
  if (BIPOLAR.has(a)) return sign(Math.round((v - 64) * 100 / 64)) + '%';
  return '' + v;
}

export const DIVISIONS = ['1/32', '1/16', '1/8', '1/4', '1/16T', '1/8T'];
export const STEPS_PER_BEAT = [8, 4, 2, 1, 6, 3];

// the A:B conditions in order: always, then 1:2, 2:2, 1:3...
export const CONDITIONS = (() => { const c = [[0, 0]]; for (let b = 2; b <= 8; ++b) for (let a = 1; a <= b; ++a) c.push([a, b]); return c; })();

// the quick access row for sound s (v: its value at an address)
export function quickFor (v) {
  const q = (a, label, choice = false, why = '') => ({ a, label, choice, why });
  const list = [q(0x02, 'Pitch 1'), q(0x11, 'Pitch 2'), q(0x27, 'Cutoff'), q(0x28, 'Reso'), q(0x3F, 'Attack'), q(0x40, 'Decay'), q(0x42, 'Release'), q(0x35, 'Volume')];
  const shape = v(0x01);
  if (shape >= 5 && shape <= 8) { list[1] = q(0x29, 'Drive', false, 'BRUIT'); list[4] = q(0x26, 'Filtre', true, 'BRUIT'); }
  else if (v(0x1B) === 0 && v(0x1C) === 0 && v(0x09) === 0) list[1] = q(0x01, 'Forme 1', true, 'SOLO');
  const pitchDrops = v(0x05) !== 64 && (v(0x07) === 0 || v(0x07) === 1);
  if (v(0x1D) > 0) { list[6] = q(0x24, 'C.Speed', false, 'CLAP'); list[7] = q(0x25, 'C.Len', false, 'CLAP'); }
  else if (pitchDrops) { list[6] = q(0x05, 'P.Mod', false, 'CHUTE'); if (v(0x07) === 1) list[7] = q(0x46, 'Decay 2', false, 'CHUTE'); }
  else if (v(0x09) > 0) list[6] = q(0x09, 'FM', false, 'FM');
  else if (v(0x58) !== 0) list[6] = q(0x59, 'Transp', false, 'NOTES');
  return list;
}

export const TRIGS = ['Vélo', 'Durée', 'Micro', 'Proba', 'Cond.', 'Retrig', 'Note'];
