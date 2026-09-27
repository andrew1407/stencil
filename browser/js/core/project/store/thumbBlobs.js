// Thumbnails as IndexedDB keeps them: the JPEG as a Blob with a signature of the data URL it came
// from, so an unchanged render is never written again. A read hands out an object URL, made once
// per Blob and revoked when the Blob is replaced or removed; the stored record is { blob, sig }.
import { hash64 } from './projectSources.js';

export const isDataUrl = (v) => typeof v === 'string' && /^data:/i.test(v);

const sigOf = (url) => `${url.length}:${hash64(url)}`;

// Synchronous, so a sync setItem can store it: base64 or percent-encoded bodies alike.
export const blobOfDataUrl = (url) => {
  const comma = url.indexOf(',');
  const head = url.slice(5, comma);
  const body = url.slice(comma + 1);
  const bytes = /;base64$/i.test(head)
    ? Uint8Array.from(atob(body), (c) => c.charCodeAt(0))
    : new TextEncoder().encode(decodeURIComponent(body));
  return new Blob([bytes], { type: head.split(';')[0] });
};

export const isThumbRecord = (v) => !!v && typeof v === 'object' && typeof v.sig === 'string' && !!v.blob;

export const createThumbMirror = (urls = URL) => {
  const entries = new Map();   // key → { blob, sig, url }
  const revoke = (e) => { if (e?.url) try { urls.revokeObjectURL(e.url); } catch { /* already gone */ } };
  return {
    has: (key) => entries.has(key),
    keys: () => entries.keys(),
    // A stored record as it was read back from IndexedDB; the same picture keeps its URL.
    adopt(key, record) {
      const cur = entries.get(key);
      if (cur && cur.sig === record.sig) return;
      revoke(cur);
      entries.set(key, { blob: record.blob, sig: record.sig, url: null });
    },
    urlOf(key) {
      const e = entries.get(key);
      if (!e) return undefined;
      return (e.url ||= urls.createObjectURL(e.blob));
    },
    // The record to persist for a data URL, or null when this one is already held.
    put(key, dataUrl) {
      const sig = sigOf(dataUrl);
      const cur = entries.get(key);
      if (cur && cur.sig === sig) return null;
      const record = { blob: blobOfDataUrl(dataUrl), sig };
      this.adopt(key, record);
      return record;
    },
    drop(key) {
      revoke(entries.get(key));
      entries.delete(key);
    },
  };
};
