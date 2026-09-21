// Controls describe themselves through `data-title`, never `title`: a live `title` also
// raises Chrome's own popup on top of ours. data-title carries no accessible name, so
// `{ label: true }` makes the first line an aria-label.

// The heading line, without the "(Alt+R)" shortcut hint or the "— reason" note.
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
