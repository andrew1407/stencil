// Pins a custom-select trigger to its widest label, measured in its own font (a px floor
// guesses at zoom and translation). Only for fixed, short option lists — arbitrary content
// would pin to its longest entry forever. Returns the width pinned (0 = nothing to measure).
export function pinToWidestOption(selectEl) {
  const wrap = selectEl && selectEl.closest ? selectEl.closest('.accent-dd') : null;
  const name = wrap ? wrap.querySelector('.accent-dd-name') : null;
  if (!name || !selectEl.options) return 0;
  const prev = selectEl.value;
  const restore = name.getAttribute('style') || '';
  // Out of the flex flow so the row's shrinking cannot shave the label. offsetWidth, not a
  // client rect: under the card's stCardIn scale(.985) a rect comes back a pixel short.
  name.style.cssText = 'position:absolute; visibility:hidden; width:max-content; max-width:none;';
  let widest = 0;
  // customSelect wraps the .value setter and never dispatches change.
  for (const opt of selectEl.options) {
    selectEl.value = opt.value;
    widest = Math.max(widest, name.offsetWidth);
  }
  selectEl.value = prev;
  name.setAttribute('style', restore);
  if (widest) name.style.minWidth = `${widest}px`;
  return widest;
}
