// ── Small DOM helpers ───────────────────────────────────────────
export const setVal = (id, value) => {
  const el = document.getElementById(id);
  if (el) el.value = value;
};
export const setRadioGroup = (name, value) => {
  document.querySelectorAll(`input[name="${name}"]`).forEach(r => { r.checked = r.value === value; });
};
// Mount an HTML string into a parent element so getElementById works
// synchronously afterwards. Appends without nuking pre-existing children.
export const mountHTML = (parent, html) => {
  parent.insertAdjacentHTML('beforeend', html);
};
// Write innerHTML only when it CHANGED, compared against the string last WRITTEN — not
// against el.innerHTML: reading an SVG back re-serializes self-closing tags, so it never
// matches and an unconditional write would rebuild (and re-animate) the nodes per poll.
const lastHtml = new WeakMap();
export const setHtml = (el, html) => {
  if (!el || lastHtml.get(el) === html) return;
  el.innerHTML = html;
  lastHtml.set(el, html);
};
// Place a fixed-position cursor-follow popup down-right of (x, y): flipped to the
// cursor's other side when it would clip, then edge-clamped. `clampY` pins an
// overflowing box to the bottom edge instead of flipping above (the projects thumb
// zoom). Shared by exportPreview.js and projectsModal.js.
export const placeNearCursor = (el, x, y, { pad = 18, edge = 8, clampY = false } = {}) => {
  const { width: w, height: h } = el.getBoundingClientRect();
  let left = x + pad;
  let top = y + pad;
  if (left + w > window.innerWidth - edge) left = x - w - pad;
  if (top + h > window.innerHeight - edge) top = clampY ? window.innerHeight - edge - h : y - h - pad;
  el.style.left = `${Math.max(edge, left)}px`;
  el.style.top = `${Math.max(edge, top)}px`;
};


// ── Notification balloon ────────────────────────────────────────
// Delegates to the <stencil-notifications> custom element, which owns the
// show/auto-hide logic. Kept as a free function so existing import sites work.
export const notify = (msg, type = 'ok', opts = undefined) => {
  const el = document.getElementById('notify-balloon');
  if (el && typeof el.notify === 'function') el.notify(msg, type, opts);
};

// Touch-like surface: phone-width viewport OR no-hover + coarse pointer. THE app-wide
// rule — never sniff the user agent. `mm` injectable for tests; no matchMedia (Node) = desktop.
export const PHONE_MEDIA = '(max-width: 680px)';
export const TOUCH_MEDIA = `${PHONE_MEDIA}, (hover: none) and (pointer: coarse)`;
export const isTouchLike = (mm = (typeof matchMedia !== 'undefined' ? matchMedia : null)) => {
  try { return !!mm && !!mm(TOUCH_MEDIA).matches; } catch { return false; }
};

// Scale any CanvasImageSource to ≤ maxEdge px on the long edge, encode as a data URL.
// Never upscales. THE shared frame for attachment downscaling, extension thumbnails,
// and video frame grabs — browser-only.
export const scaledDataUrl = (source, width, height, maxEdge, type, quality) => {
  const k = Math.min(1, maxEdge / Math.max(width, height));
  const c = document.createElement('canvas');
  c.width = Math.max(1, Math.round(width * k));
  c.height = Math.max(1, Math.round(height * k));
  c.getContext('2d').drawImage(source, 0, 0, c.width, c.height);
  return c.toDataURL(type, quality);
};

// Whether the browser can share FILES via the Web Share API (most desktop browsers
// cannot, even if navigator.share exists for text/URLs); gates the Share-image action.
export const supportsShareFiles = () => {
  try {
    return !!(navigator.canShare &&
      navigator.canShare({ files: [new File([], 'x.png', { type: 'image/png' })] }));
  } catch {
    return false;
  }
};


// The native colour picker opens beside the input's own box, so a hidden 1px input pops
// it at the page corner instead of at the button pressed. Pin the input under the button
// first. Used by every swatch that hides its input behind a button.
export const anchorPickerInput = (input, btn) => {
  const r = btn?.getBoundingClientRect?.();
  if (!r) return;
  input.style.position = 'fixed';
  input.style.left = `${Math.round(r.left)}px`;
  input.style.top = `${Math.round(r.bottom)}px`;
};


// Text-entry target (global hotkeys suppressed): textareas, text-like inputs, selects,
// contentEditable. Checkbox/radio/file/color/button inputs are NOT typing targets.
export const isTypingTarget = t => {
  if (!t) return false;
  const tag = (t.tagName || '').toLowerCase();
  if (tag === 'textarea') return true;
  if (tag === 'select') return true;
  if (tag === 'input') {
    const ty = (t.type || '').toLowerCase();
    return !(ty === 'checkbox' || ty === 'radio' || ty === 'file' || ty === 'color' || ty === 'button');
  }
  return t.isContentEditable === true;
};

// Copy shortcuts (Ctrl+C = image, Ctrl+Alt+C = layout) defer to the browser's native
// text copy while a non-empty selection exists, so Ctrl+C copies the TEXT, not the image.
export const hasTextSelection = () => {
  if (typeof window === 'undefined' || !window.getSelection) return false;
  const sel = window.getSelection();
  return !!sel && !sel.isCollapsed && sel.toString().trim().length > 0;
};
