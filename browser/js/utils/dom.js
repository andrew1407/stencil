export const setVal = (id, value) => {
  const el = document.getElementById(id);
  if (el) el.value = value;
};
export const setRadioGroup = (name, value) => {
  document.querySelectorAll(`input[name="${name}"]`).forEach(r => { r.checked = r.value === value; });
};
// Appends without nuking pre-existing children.
export const mountHTML = (parent, html) => {
  parent.insertAdjacentHTML('beforeend', html);
};
// Compared against the string last WRITTEN, not el.innerHTML: an SVG re-serializes its
// self-closing tags, so it never matches and every poll would rebuild (and re-animate).
const lastHtml = new WeakMap();
export const setHtml = (el, html) => {
  if (!el || lastHtml.get(el) === html) return;
  el.innerHTML = html;
  lastHtml.set(el, html);
};
// Down-right of (x, y), flipped to the other side when it would clip, then edge-clamped.
// `clampY` pins an overflowing box to the bottom edge instead of flipping above.
export const placeNearCursor = (el, x, y, { pad = 18, edge = 8, clampY = false } = {}) => {
  const { width: w, height: h } = el.getBoundingClientRect();
  let left = x + pad;
  let top = y + pad;
  if (left + w > window.innerWidth - edge) left = x - w - pad;
  if (top + h > window.innerHeight - edge) top = clampY ? window.innerHeight - edge - h : y - h - pad;
  el.style.left = `${Math.max(edge, left)}px`;
  el.style.top = `${Math.max(edge, top)}px`;
};


// Delegates to <stencil-notifications>; kept as a free function for the import sites.
export const notify = (msg, type = 'ok', opts = undefined) => {
  const el = document.getElementById('notify-balloon');
  if (el && typeof el.notify === 'function') el.notify(msg, type, opts);
};

// THE app-wide touch rule — never sniff the user agent. No matchMedia (Node) = desktop.
export const PHONE_MEDIA = '(max-width: 680px)';
export const TOUCH_MEDIA = `${PHONE_MEDIA}, (hover: none) and (pointer: coarse)`;
export const isTouchLike = (mm = (typeof matchMedia !== 'undefined' ? matchMedia : null)) => {
  try { return !!mm && !!mm(TOUCH_MEDIA).matches; } catch { return false; }
};

// ≤ maxEdge px on the long edge, never upscaled; THE shared frame for attachment
// downscaling, extension thumbnails and video frame grabs.
export const scaledDataUrl = (source, width, height, maxEdge, type, quality) => {
  const k = Math.min(1, maxEdge / Math.max(width, height));
  const c = document.createElement('canvas');
  c.width = Math.max(1, Math.round(width * k));
  c.height = Math.max(1, Math.round(height * k));
  c.getContext('2d').drawImage(source, 0, 0, c.width, c.height);
  return c.toDataURL(type, quality);
};

// Most desktop browsers cannot share FILES even when navigator.share exists.
export const supportsShareFiles = () => {
  try {
    return !!(navigator.canShare &&
      navigator.canShare({ files: [new File([], 'x.png', { type: 'image/png' })] }));
  } catch {
    return false;
  }
};


// The native colour picker opens beside the input's own box, so a hidden 1px input pops
// it at the page corner unless it is pinned under the button first.
export const anchorPickerInput = (input, btn) => {
  const r = btn?.getBoundingClientRect?.();
  if (!r) return;
  input.style.position = 'fixed';
  input.style.left = `${Math.round(r.left)}px`;
  input.style.top = `${Math.round(r.bottom)}px`;
};


// Checkbox/radio/file/color/button inputs are NOT typing targets.
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

// Ctrl+C with a non-empty text selection copies the TEXT, not the image.
export const hasTextSelection = () => {
  if (typeof window === 'undefined' || !window.getSelection) return false;
  const sel = window.getSelection();
  return !!sel && !sel.isCollapsed && sel.toString().trim().length > 0;
};
