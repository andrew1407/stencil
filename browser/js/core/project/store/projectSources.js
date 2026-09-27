// A data-URL image source as the registry keeps it: a compact reference (media type, length and a
// 64-bit hash of the whole URL), stable across tabs and boots so the same image still dedupes. The
// full URL stays in the project's own payload (layout.imageSource, in IndexedDB) and is resolved
// from there only where a caller needs the text.

export const SOURCE_REF_SCHEME = 'stencil-source:';

export const isDataUrlSource = (s) => typeof s === 'string' && /^data:/i.test(s);

export const isSourceRef = (s) => typeof s === 'string' && s.startsWith(SOURCE_REF_SCHEME);

// cyrb53's mix with both 32-bit lanes kept: 64 bits over every UTF-16 unit, as 16 hex digits.
export const hash64 = (s) => {
  let h1 = 0xdeadbeef ^ s.length;
  let h2 = 0x41c6ce57 ^ s.length;
  for (let i = 0; i < s.length; i++) {
    const c = s.charCodeAt(i);
    h1 = Math.imul(h1 ^ c, 2654435761);
    h2 = Math.imul(h2 ^ c, 1597334677);
  }
  h1 = Math.imul(h1 ^ (h1 >>> 16), 2246822507) ^ Math.imul(h2 ^ (h2 >>> 13), 3266489909);
  h2 = Math.imul(h2 ^ (h2 >>> 16), 2246822507) ^ Math.imul(h1 ^ (h1 >>> 13), 3266489909);
  const hex = (h) => (h >>> 0).toString(16).padStart(8, '0');
  return hex(h2) + hex(h1);
};

const mediaType = (s) => (/^data:([\w.+-]{1,64}\/[\w.+-]{1,64})/i.exec(s.slice(0, 160))?.[1] || '').toLowerCase();

// A save re-derives the same session's reference, so the last one is kept.
let last = { url: null, ref: null };

// A data URL's reference; any other value as it is, so a reference maps to itself.
export const sourceRef = (s) => {
  if (!isDataUrlSource(s)) return s;
  if (last.url !== s) last = { url: s, ref: `${SOURCE_REF_SCHEME}${mediaType(s)};${s.length};${hash64(s)}` };
  return last.ref;
};

export const withSourceRef = (meta) => {
  if (isDataUrlSource(meta.source)) meta.source = sourceRef(meta.source);
  return meta;
};

// The text a stored source stands for: the payload's full URL when the reference is its, else the
// stored value (a plain URL, or a reference whose text was shed).
export const resolveSource = (stored, full) =>
  (isSourceRef(stored) && isDataUrlSource(full) && sourceRef(full) === stored ? full : stored);

export const storedSource = (store, id) => {
  const meta = store.getMeta(id);
  if (!meta || !isSourceRef(meta.source)) return meta?.source ?? null;
  return resolveSource(meta.source, store.get(id)?.payload?.layout?.imageSource);
};

// The payload with a data-URL layout.imageSource down to its reference — what a quota failure gives
// up first, since the image and the dedupe survive it; the same object when there is nothing to shed.
export const shedSource = (payload) => {
  const src = payload?.layout?.imageSource;
  return isDataUrlSource(src) ? { ...payload, layout: { ...payload.layout, imageSource: sourceRef(src) } } : payload;
};

// What leaves this browser as a source — a file, a hand-off, the facade, the Links field: a
// reference only this registry can resolve goes out as no source.
export const portableSource = (s) => (isSourceRef(s) ? '' : s);

// A server row's source over the local entry for the same project: the server's when it is a URL,
// else the local one, which the server was never sent.
export const keptSource = (serverSrc, localSrc) =>
  (/^https?:/i.test(serverSrc || '') ? serverSrc : (localSrc || serverSrc || ''));

// What a server may be sent: the server keeps the bytes as the original, so never a data URL, and
// never a reference only this browser can resolve.
export const wireSource = (s) => (typeof s === 'string' && !isDataUrlSource(s) && !isSourceRef(s) ? s : '');

// A payload without the text gets it before its row drops it; one that cannot be read or written
// keeps its image, and the reference still dedupes.
const keepInPayload = (storage, key, url) => {
  try {
    const payload = JSON.parse(storage.getItem(key));
    const layout = payload?.layout;
    if (!layout || typeof layout !== 'object' || layout.imageSource) return;
    layout.imageSource = url;
    storage.setItem(key, JSON.stringify(payload));
  } catch { /* the text is shed */ }
};

// A registry an older build wrote carries data-URL sources inline; each becomes its reference, in
// place. True when a row changed.
export const moveInlineSources = (storage, rows, payloadKey) => {
  let moved = false;
  for (const m of rows) {
    if (!m || m.id == null || !isDataUrlSource(m.source)) continue;
    keepInPayload(storage, payloadKey(m.id), m.source);
    m.source = sourceRef(m.source);
    moved = true;
  }
  return moved;
};
