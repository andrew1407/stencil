// Project images as IndexedDB keeps them: a Blob and a signature, as thumbBlobs.js keeps thumbnails,
// so a boot holds Blob handles rather than every image. A read hands out one object URL per Blob, or
// the data URL itself for the one key this tab holds (last written or `materialize`d): the editor
// compares and re-saves that very string, and exports and hand-offs read it back.
import { isDataUrl } from './thumbBlobs.js';

export const isImageRecord = (v) => !!v && typeof v === 'object' && typeof v.sig === 'string' && !!v.blob;

// Names one write, not its bytes: hashing a multi-megabyte data URL on every change is the cost avoided.
const freshSig = (v) => `${v.length}:${Date.now().toString(36)}${Math.random().toString(36).slice(2, 8)}`;

const base64Of = (bytes) => {
  let s = '';
  for (let i = 0; i < bytes.length; i += 0x8000) s += String.fromCharCode(...bytes.subarray(i, i + 0x8000));
  return btoa(s);
};

// FileReader in a page; the bytes through btoa where there is none (node --test).
export const dataUrlOfBlob = async (blob) => {
  if (typeof FileReader !== 'function') {
    return `data:${blob.type || 'application/octet-stream'};base64,${base64Of(new Uint8Array(await blob.arrayBuffer()))}`;
  }
  return new Promise((resolve, reject) => {
    const r = new FileReader();
    r.onload = () => resolve(r.result);
    r.onerror = () => reject(r.error);
    r.readAsDataURL(blob);
  });
};

export const createImageMirror = (urls = URL) => {
  const entries = new Map();   // key → { sig, blob, url, src }
  let held = null;
  const revoke = (e) => { if (e?.url) try { urls.revokeObjectURL(e.url); } catch { /* already gone */ } };
  const place = (key, e) => {
    const cur = entries.get(key);
    if (cur !== e) revoke(cur);
    entries.set(key, e);
    return e;
  };
  // One key keeps its data URL; the one before lets go of it once it has its Blob to fall back on.
  const hold = (key, e, src) => {
    const prev = held !== key && entries.get(held);
    if (prev?.blob) prev.src = null;
    held = key;
    e.src = src;
  };
  const release = (key, e) => { if (held !== key && e.blob) e.src = null; };

  return {
    has: (key) => entries.has(key),
    keys: () => entries.keys(),
    // What IndexedDB holds for the key: a record, or the data-URL string an older build wrote.
    adopt(key, v) {
      const cur = entries.get(key);
      if (isImageRecord(v)) {
        if (cur && cur.sig === v.sig) { cur.blob ||= v.blob; release(key, cur); return; }
        place(key, { sig: v.sig, blob: v.blob, url: null, src: null });
      } else if (typeof v === 'string' && !(cur && cur.src === v)) {
        place(key, { sig: null, blob: null, url: null, src: v });
      }
    },
    read(key) {
      const e = entries.get(key);
      if (!e) return undefined;
      if (e.src != null) return e.src;
      return (e.url ||= urls.createObjectURL(e.blob));
    },
    // What to persist for `v`, as a promise (the Blob decodes off the save; null once superseded),
    // or null when the key holds it already. An object URL this mirror handed out copies its image.
    put(key, v) {
      const cur = entries.get(key);
      if (cur && (cur.src === v || (cur.url && cur.url === v))) return null;
      if (!isDataUrl(v)) {
        const from = [...entries.values()].find((e) => e.url === v && e.blob);
        if (!from) return Promise.resolve(place(key, { sig: null, blob: null, url: null, src: v }).src);
        if (cur && cur.sig === from.sig) return null;
        place(key, { sig: from.sig, blob: from.blob, url: null, src: null });
        return Promise.resolve({ blob: from.blob, sig: from.sig });
      }
      const e = place(key, { sig: freshSig(v), blob: null, url: null, src: null });
      hold(key, e, v);
      return fetch(v).then((r) => r.blob()).then((blob) => {
        if (entries.get(key) !== e) return null;
        e.blob = blob;
        release(key, e);
        return { blob, sig: e.sig };
      });
    },
    // The key's image as a data URL; `keep` makes it the held one, as an opened project is.
    async materialize(key, keep = true) {
      const e = entries.get(key);
      if (!e) return null;
      const src = e.src ?? await dataUrlOfBlob(e.blob);
      if (keep && entries.get(key) === e) hold(key, e, src);
      return src;
    },
    drop(key) {
      revoke(entries.get(key));
      entries.delete(key);
      if (held === key) held = null;
    },
  };
};
