// ── Tooltip text, without the native popup ──────────────────────────────────
// Controls describe themselves through `data-title`, never `title`: lib/controlTooltip.js
// reads either, but a live `title` ALSO raises Chrome's own popup — slow, unstyled, and
// on top of ours. So nothing in the extension's own UI sets `title` on an element.
//
// `title` did double duty as the accessible name of an icon-only control, and data-title
// carries no such meaning — pass `{ label: true }` there and the first line becomes an
// aria-label. Pure DOM, no imports; safe with a null element.

// The name a screen reader should read: the tooltip's heading line, without the trailing
// "(Alt+R)" shortcut hint or the "— reason" note that only make sense on screen.
export const tipLabel = (text) => String(text == null ? '' : text)
  .split('\n')[0]
  .replace(/\s*\([^()]*\)\s*$/, '')
  .split(' — ')[0]
  .trim();

export const setTip = (el, text, { label = false } = {}) => {
  if (!el || !el.setAttribute) return el;
  const s = text == null ? '' : String(text);
  if (s) el.setAttribute('data-title', s);
  else el.removeAttribute('data-title');
  if (label) {
    const name = tipLabel(s);
    if (name) el.setAttribute('aria-label', name);
    else el.removeAttribute('aria-label');
  }
  return el;
};
