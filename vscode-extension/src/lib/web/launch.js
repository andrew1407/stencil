// The `#stencil=` hand-off the browser app boots on: the URL browser-extension's
// editorLaunch.js writes, the payload deepLink.js validates, plus a `script` it ignores.
import { readFileSync, statSync } from 'node:fs';
import { basename, extname } from 'node:path';

// Past this, Chrome drops the navigation — the real ceiling, under the validator's 32 MiB.
// The same number browser-extension/src/lib/menu/editorLaunch.js writes; the tests pin the pair.
const MAX_PAYLOAD = 1_800_000;
// Bytes of a local image whose base64 (4 chars per 3 bytes) still fits MAX_PAYLOAD; both the
// hand-off and the console route refuse a larger file before reading it.
const MAX_INLINE_BYTES = Math.floor((MAX_PAYLOAD * 3) / 4);

// An unreadable path fits: the read itself reports it.
const fitsInline = (path) => {
  try { return statSync(path).size <= MAX_INLINE_BYTES; } catch { return true; }
};

const IMAGE_TYPES = Object.freeze({
  '.png': 'image/png', '.jpg': 'image/jpeg', '.jpeg': 'image/jpeg', '.webp': 'image/webp',
  '.gif': 'image/gif', '.bmp': 'image/bmp', '.avif': 'image/avif',
  '.tif': 'image/tiff', '.tiff': 'image/tiff',
});

const buildLaunchUrl = (base, payload) =>
  `${String(base ?? '').split('#')[0]}#stencil=${encodeURIComponent(JSON.stringify(payload))}`;

const isRemote = (spec) => /^https?:\/\//i.test(String(spec ?? ''));

// Null for an unreadable file or an unknown type; the bytes ride the fragment, unseen.
const imageDataUrl = (path) => {
  const type = IMAGE_TYPES[extname(String(path ?? '')).toLowerCase()];
  if (!type || !fitsInline(path)) return null;
  try {
    return { dataUrl: `data:${type};base64,${readFileSync(path).toString('base64')}`, name: basename(path) };
  } catch {
    return null;
  }
};

// Null for a spec that starts like a URL and is not one, as an unreadable file is null.
const remotePart = (image) => {
  try {
    return { src: image, name: basename(new URL(image).pathname) || 'image.png' };
  } catch {
    return null;
  }
};

// A local path inlined, an http(s) one named; `inline` off keeps local bytes out.
const imagePart = (image, { inline = true } = {}) => {
  if (!image) return null;
  if (isRemote(image)) return remotePart(image);
  return inline ? imageDataUrl(image) : null;
};

// What the browser cannot open: no filesystem, so only a URL is fetchable (stc §10).
const localSources = (blocks) => (blocks ?? [])
  .filter((block) => block && block.source && block.kind !== 'url')
  .map((block) => block.source);

const scriptLaunch = (script, image, opts) => {
  const payload = { script: String(script ?? '') };
  return Object.assign(payload, imagePart(image, opts) ?? {});
};

// A saved .stencil is already the fragment's parts: image → dataUrl, export layout → layout.
const projectLaunch = (text) => {
  let doc;
  try {
    doc = JSON.parse(String(text ?? ''));
  } catch {
    return null;
  }
  const dataUrl = doc && doc.image && typeof doc.image.dataUrl === 'string' ? doc.image.dataUrl : '';
  if (!dataUrl.startsWith('data:')) return null;
  // A .stencil stores `ext` bare ("png"); a dotted one is taken as it is.
  const ext = String(doc.image.ext || 'png');
  const name = `${doc.name || 'project'}${ext.startsWith('.') ? ext : `.${ext}`}`;
  return doc.layout ? { dataUrl, name, layout: doc.layout } : { dataUrl, name };
};

const isTooBig = (url) => String(url ?? '').length > MAX_PAYLOAD;

export {
  IMAGE_TYPES, MAX_INLINE_BYTES, MAX_PAYLOAD, buildLaunchUrl, fitsInline, imageDataUrl, imagePart, isRemote,
  isTooBig, localSources, projectLaunch, scriptLaunch,
};
