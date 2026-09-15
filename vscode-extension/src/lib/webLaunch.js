// The `#stencil=` hand-off the browser app boots on: the URL browser-extension's
// editorLaunch.js writes, the payload deepLink.js validates, plus a `script` it ignores.
'use strict';

const { readFileSync } = require('node:fs');
const { basename, extname } = require('node:path');

// Past this, Chrome drops the navigation — the real ceiling, under the validator's 32 MiB.
const MAX_PAYLOAD = 1800000;

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
  if (!type) return null;
  try {
    return { dataUrl: `data:${type};base64,${readFileSync(path).toString('base64')}`, name: basename(path) };
  } catch {
    return null;
  }
};

// A local path inlined, an http(s) one named; `inline` off keeps local bytes out.
const imagePart = (image, { inline = true } = {}) => {
  if (!image) return null;
  if (isRemote(image)) return { src: image, name: basename(new URL(image).pathname) || 'image.png' };
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
  const name = `${doc.name || 'project'}${doc.image.ext || '.png'}`;
  return doc.layout ? { dataUrl, name, layout: doc.layout } : { dataUrl, name };
};

const tooBig = (url) => String(url ?? '').length > MAX_PAYLOAD;

module.exports = {
  IMAGE_TYPES, MAX_PAYLOAD, buildLaunchUrl, imageDataUrl, imagePart, isRemote,
  localSources, projectLaunch, scriptLaunch, tooBig,
};
