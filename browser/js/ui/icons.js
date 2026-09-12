// One inline-SVG source for every glyph: stroked line-art on a 24×24 grid in `currentColor`.
// Paths live in config/icons.json; extension/src/lib/icons.js mirrors it — keep in sync.
// Pure strings: icon() output contains no backtick or "${" (the markup tests assert that).
import ICONS_DATA from '../config/icons.json' with { type: 'json' };
import SVG_ART from '../config/svgArt.json' with { type: 'json' };

// 'eraser' wipes drawn lines and is deliberately not the trash can (trash = delete). The
// class="ic-…" hooks on glyph parts are inert here; config/iconMotion.json moves them via
// css/animations/iconHover.css.
export const ICONS = ICONS_DATA;

// The draw-mode toggle's two faces: complete <svg> strings on a 16-grid, not built by icon().
export const DRAW_MODE_ICON = SVG_ART.drawMode;

// Returns '' for an unknown name, so a typo degrades to no glyph during markup assembly.
export function icon(name, { size = 16, cls = '', sw = 2 } = {}) {
  const inner = ICONS[name];
  if (!inner) return '';
  const extra = cls ? ` ${cls}` : '';
  return `<svg class="ic ic-${name}${extra}" viewBox="0 0 24 24" width="${size}" height="${size}" ` +
    `fill="none" stroke="currentColor" stroke-width="${sw}" stroke-linecap="round" ` +
    `stroke-linejoin="round" aria-hidden="true" focusable="false">${inner}</svg>`;
}

// The Select all ↔ Deselect all face (desktop twin: updateSelectAll): a check gathers, a
// cross lets go. `all` is "everything on view is already checked".
export const setSelectAllFace = (btn, all) => {
  if (!btn) return;
  const label = btn.querySelector?.('span');
  if (label) label.textContent = all ? 'Deselect all' : 'Select all';
  const ic = btn.querySelector?.('.ic');
  const want = all ? 'x' : 'check';
  if (ic && !ic.classList?.contains(`ic-${want}`)) ic.outerHTML = icon(want, { size: 13 });
};
