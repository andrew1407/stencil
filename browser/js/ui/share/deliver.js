// How a Copy or a Share entry hands something on. The clipboard write starts inside the click with
// a Promise-valued ClipboardItem, so a picture or a server's layout may still be loading (macOS
// WebKit drops the activation across an await); a share is the Web Share API, a copy where it has none.
import { notify } from '../../utils.js';
import { pngOf } from './renderProject.js';

const nav = () => globalThis.navigator ?? {};
const itemWrites = () => typeof globalThis.ClipboardItem === 'function' && !!nav().clipboard?.write;

// One outcome promise: said once, already observed, still rejecting for a caller that awaits it.
const settle = (work, label, what) => {
  const out = work.then(() => { notify(label, 'ok'); }, (err) => {
    if (err?.name !== 'AbortError') notify(`${what} failed — ${err?.message || err}`, 'fail');
    throw err;
  });
  out.catch(() => {});
  return out;
};
const attempt = (run) => { try { return Promise.resolve(run()); } catch (err) { return Promise.reject(err); } };

export const copyText = (text, label = 'Copied to clipboard') => settle(attempt(() => {
  const t = Promise.resolve(text);
  if (itemWrites()) {
    const blob = t.then((s) => new Blob([String(s)], { type: 'text/plain' }));
    return nav().clipboard.write([new globalThis.ClipboardItem({ 'text/plain': blob })]);
  }
  return t.then((s) => nav().clipboard.writeText(String(s)));
}), label, 'Copy');

// The clipboard takes PNG everywhere; anything else is re-encoded through a canvas.
const asPng = async (blob) => {
  if (blob.type === 'image/png') return blob;
  const bmp = await createImageBitmap(blob);
  try {
    const canvas = document.createElement('canvas');
    canvas.width = bmp.width;
    canvas.height = bmp.height;
    canvas.getContext('2d').drawImage(bmp, 0, 0);
    return await pngOf(canvas);
  } finally { bmp.close(); }
};

export const copyImage = (blob, label = 'Image copied to clipboard') => settle(attempt(() =>
  nav().clipboard.write([new globalThis.ClipboardItem({ 'image/png': Promise.resolve(blob).then(asPng) })])), label, 'Copy');

export const shareLink = (url, title = 'Stencil') => {
  if (typeof nav().share !== 'function') return copyText(url, 'Link copied — this browser has no share sheet');
  return settle(attempt(() => nav().share({ title, url })), 'Shared', 'Share');
};

// Decided inside the click, before any await: a browser that cannot share files copies instead.
const sharesFiles = () => {
  try { return !!nav().canShare?.({ files: [new File([''], 'probe.png', { type: 'image/png' })] }); } catch { return false; }
};
export const shareImage = (blob, name = 'image.png', title = 'Stencil') => {
  if (!sharesFiles()) return copyImage(blob, 'Image copied — this browser cannot share pictures');
  return settle(attempt(async () => {
    const b = await blob;
    return nav().share({ files: [new File([b], name, { type: b.type || 'image/png' })], title });
  }), 'Shared', 'Share');
};
