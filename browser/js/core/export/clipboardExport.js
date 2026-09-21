// Clipboard exports. Both keep one outcome-promise shape: rejections are pre-caught on a
// side branch so a fire-and-forget caller never trips unhandledrejection, while an
// awaiting caller (the §10 `copy` op) still observes the real result.
import { notify, isSplitCompare } from '../../utils.js';
import { VARIANT_META } from '../image/imageVariants.js';

// write() MUST run synchronously inside the Cmd/Ctrl+C gesture with a Promise-valued
// ClipboardItem: deferring into toBlob loses user activation (NotAllowedError, macOS WebKit).
export const copyImageToClipboard = (svc, variant = 'current') => {
  const outcome = (() => {
    const app = svc.app;
    if (!app.image) { notify('No image to copy', 'fail'); return Promise.reject(new Error('No image to copy')); }
    if (variant === 'split' && !isSplitCompare(app)) {
      notify('Turn on split compare to copy with the splitter', 'fail');
      return Promise.reject(new Error('Split compare is not active'));
    }
    try {
// 'split' is its own explicit variant — no substitution happens here.
      const off = svc.renderExportCanvas(variant);
      const blobP = new Promise((res, rej) =>
        off.toBlob(b => b ? res(b) : rej(new Error('Image encode failed')), 'image/png'));
      const label = VARIANT_META[variant]?.copyLabel || 'Image copied to clipboard';
      return navigator.clipboard.write([new ClipboardItem({ 'image/png': blobP })])
        .then(() => notify(label, 'ok'))
        .catch(err => { notify('Copy failed: ' + (err.message || err), 'fail'); throw err; });
    } catch (e) {
      notify('Copy failed: ' + e.message, 'fail');
      return Promise.reject(e);
    }
  })();
  outcome.catch(() => { /* observed above; awaiting callers re-observe */ });
  return outcome;
};

// The FULL layout via currentLayoutPayload (lines plus every applied edit), so a paste
// reproduces the whole editor state.
export const copyLayoutToClipboard = (svc) => {
  const outcome = (() => {
    const app = svc.app;
    if (!app.lines || app.lines.length === 0) {
      notify('No layout to copy', 'fail');
      return Promise.reject(new Error('No layout to copy'));
    }
    const txt = JSON.stringify(app.currentLayoutPayload(), null, 2);
    return navigator.clipboard.writeText(txt)
      .then(() => notify('Layout JSON copied', 'ok'))
      .catch(err => { notify('Copy failed: ' + (err.message || err), 'fail'); throw err; });
  })();
  outcome.catch(() => { /* observed above; awaiting callers re-observe */ });
  return outcome;
};
