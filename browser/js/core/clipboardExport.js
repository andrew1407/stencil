// ── Clipboard exports: the edited image, and the layout as JSON text ────────
// Both keep the same outcome-promise shape: rejections are pre-caught on a side branch so
// a fire-and-forget caller never trips unhandledrejection, while an awaiting caller (the
// §10 `copy` op) still observes the real result.
import { notify, isSplitCompare } from '../utils.js';
import { VARIANT_META } from './imageVariants.js';

// ── Clipboard: copy the current image (with active filter) ──
// The write() MUST run synchronously inside the Cmd/Ctrl+C gesture with a Promise-valued
// ClipboardItem — deferring into the async toBlob callback loses the user-activation
// (NotAllowedError on macOS WebKit). Returns a promise resolving on a successful write
// and REJECTING on failure, so a plan-driven copy (§10 `copy` op) can report the outcome.
//   variant: 'current' (Ctrl+C) | 'original' (Ctrl+Shift+C) | 'tint' (Ctrl+Alt+C) |
//            'split' (Ctrl+C's own slot in a compare view; the call site decides which).
export const copyImageToClipboard = (svc, variant = 'current') => {
  // Rejections are PRE-CAUGHT on a side branch so a fire-and-forget caller (the
  // toolbar button, the chainable facade) never trips unhandledrejection, while an
  // awaiting caller (the §10 copy op) still observes the real outcome.
  const outcome = (() => {
    const app = svc.app;
    if (!app.image) { notify('No image to copy', 'fail'); return Promise.reject(new Error('No image to copy')); }
    if (variant === 'split' && !isSplitCompare(app)) {
      notify('Turn on split compare to copy with the splitter', 'fail');
      return Promise.reject(new Error('Split compare is not active'));
    }
    try {
      // 'current' is always the plain edited frame; 'split' is its own explicit variant
      // (see the class-level comment above) — no substitution happens in here.
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

// ── Clipboard: copy layout JSON text ──
// Copies the FULL layout — lines plus every applied edit (filter/tint, crop, rotation, page
// format, formulas) via currentLayoutPayload, so a paste reproduces the whole editor state.
export const copyLayoutToClipboard = (svc) => {
  // Same outcome-promise shape as copyImageToClipboard: rejections are pre-caught
  // on a side branch so fire-and-forget callers (toolbar, chainable facade) never
  // trip unhandledrejection, while the §10 copy op still observes the real outcome.
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
