// Waldorf's own files for the RackAttack (factory sets, bonus kits, patterns), read in the browser: zips (and the
// zips inside them), MIDI files, .syx. Nothing goes to the site: what is read stays in this browser (IndexedDB),
// for the next visits.

//==============================================================================
// zip files: their entries (stored or deflated), with the zips inside them opened too
export async function unzip (bytes, set, depth = 0) {
  const out = [];
  const v = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  let end = -1;
  for (let i = bytes.length - 22; i >= Math.max(0, bytes.length - 22 - 65535); --i)
    if (v.getUint32(i, true) === 0x06054b50) { end = i; break; }
  if (end < 0) return out;
  const count = v.getUint16(end + 10, true);
  let at = v.getUint32(end + 16, true);
  for (let n = 0; n < count && at + 46 <= bytes.length; ++n) {
    if (v.getUint32(at, true) !== 0x02014b50) break;
    const flags = v.getUint16(at + 8, true), method = v.getUint16(at + 10, true);
    const size = v.getUint32(at + 20, true), nameLen = v.getUint16(at + 28, true);
    const extraLen = v.getUint16(at + 30, true), commentLen = v.getUint16(at + 32, true), local = v.getUint32(at + 42, true);
    const path = new TextDecoder().decode(bytes.subarray(at + 46, at + 46 + nameLen)).replace(/\\/g, '/');
    at += 46 + nameLen + extraLen + commentLen;
    const leaf = path.slice(path.lastIndexOf('/') + 1);
    if (!leaf || path.includes('__MACOSX') || leaf.startsWith('._') || (flags & 1)) continue;   // folders, Mac leftovers, locked
    const ext = leaf.includes('.') ? leaf.slice(leaf.lastIndexOf('.') + 1).toLowerCase() : '';
    if (!['zip', 'mid', 'midi', 'syx'].includes(ext)) continue;
    if (v.getUint32(local, true) !== 0x04034b50) continue;
    const start = local + 30 + v.getUint16(local + 26, true) + v.getUint16(local + 28, true);
    const raw = bytes.subarray(start, start + size);
    let data = null;
    try { data = method === 0 ? raw.slice() : method === 8 ? await inflate(raw) : null; } catch (e) { data = null; }
    if (!data) continue;
    const stem = leaf.slice(0, leaf.length - ext.length - 1);
    if (ext === 'zip') { if (depth < 4) out.push(...await unzip(data, stem, depth + 1)); }   // a zip inside: its own set
    else out.push({ set, stem, data });
  }
  return out;
}

async function inflate (raw) {
  const ds = new DecompressionStream('deflate-raw');
  const stream = new Blob([raw]).stream().pipeThrough(ds);
  return new Uint8Array(await new Response(stream).arrayBuffer());
}

//==============================================================================
// the kits kept in this browser: { id: set + '/' + name, set, name, number, kit (it has sounds), patterns, syx }
const DB = 'rackattack', STORE = 'kits';
function open () {
  return new Promise((resolve, reject) => {
    const r = indexedDB.open(DB, 1);
    r.onupgradeneeded = () => r.result.createObjectStore(STORE, { keyPath: 'id' });
    r.onsuccess = () => resolve(r.result);
    r.onerror = () => reject(r.error);
  });
}
async function run (mode, fn) {
  const db = await open();
  try {
    return await new Promise((resolve, reject) => {
      const t = db.transaction(STORE, mode), s = t.objectStore(STORE);
      const r = fn(s);
      t.oncomplete = () => resolve(r && 'result' in r ? r.result : undefined);
      t.onerror = () => reject(t.error);
      t.onabort = () => reject(t.error);
    });
  } finally { db.close(); }
}
export const kitStore = {
  all: () => run('readonly', s => s.getAll()).then(list => list || []),
  put: (records) => run('readwrite', s => { for (const r of records) s.put(r); }),
  removeSet: async (set) => {
    const list = await kitStore.all();
    return run('readwrite', s => { for (const r of list) if (r.set === set) s.delete(r.id); });
  },
  clear: () => run('readwrite', s => s.clear()),
};
