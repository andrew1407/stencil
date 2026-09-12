// ── Pin a custom-select trigger to its widest option ─────────────────────────
// A dropdown that changes width with its value shoves the rest of the row about, so it
// gets pinned to the WIDEST label the list can show, measured in the trigger's own font
// (a px floor is a guess at another font size, zoom level or translation).
// Only for lists whose options are FIXED and short: one carrying arbitrary content (a
// hostname, a project name) would pin itself to its longest entry forever.

// Returns the width it pinned (0 when there was nothing to measure).
export function pinToWidestOption(selectEl) {
  const wrap = selectEl && selectEl.closest ? selectEl.closest('.accent-dd') : null;
  const name = wrap ? wrap.querySelector('.accent-dd-name') : null;
  if (!name || !selectEl.options) return 0;
  const prev = selectEl.value;
  const restore = name.getAttribute('style') || '';
  // Out of the flex flow at max-content for the walk, so the row's own shrinking can't
  // shave a fraction off the label being measured. offsetWidth, not a client rect: this
  // runs while the card still rides its stCardIn entrance (lib/animations/keyframes.css), and a
  // rect measured under that scale(.985) comes back a pixel short — permanently.
  name.style.cssText = 'position:absolute; visibility:hidden; width:max-content; max-width:none;';
  let widest = 0;
  // Setting .value only re-syncs the label (customSelect wraps the setter and never
  // dispatches change), and the walk is one frame, so nothing paints mid-walk.
  for (const opt of selectEl.options) {
    selectEl.value = opt.value;
    widest = Math.max(widest, name.offsetWidth);
  }
  selectEl.value = prev;
  name.setAttribute('style', restore);
  if (widest) name.style.minWidth = `${widest}px`;
  return widest;
}
