// The accent the page shows right now, a hover preview included (accent/controller.js writes
// data-accent, or the inline --accent of a custom colour, with no event): what a session skin's
// own colours follow, called only when that colour moves. A no-op without a MutationObserver.
import { accentHex, normalizeHex } from '../../core/settings/accents.js';

export const shownAccentHex = (doc = document) => {
  const root = doc.documentElement;
  return normalizeHex(root.style?.getPropertyValue?.('--accent')?.trim?.()) || accentHex(root.getAttribute('data-accent'));
};

export const watchShownAccent = (doc, onShown) => {
  if (typeof MutationObserver !== 'function') return () => {};
  let last = shownAccentHex(doc);
  const observer = new MutationObserver(() => {
    const hex = shownAccentHex(doc);
    if (hex !== last) onShown((last = hex));
  });
  observer.observe(doc.documentElement, { attributes: true, attributeFilter: ['data-accent', 'style'] });
  return () => observer.disconnect();
};
