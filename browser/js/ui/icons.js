// One inline-SVG source for every glyph: stroked line-art on a 24×24 grid in `currentColor`.
// Paths live in config/icons.json; browser-extension/src/lib/icons.js mirrors it — keep in sync.
// Pure strings: icon() output contains no backtick or "${" (the markup tests assert that).
import ICONS_DATA from '../config/icons.json' with { type: 'json' };
import SVG_ART from '../config/svgArt.json' with { type: 'json' };

// 'eraser' wipes drawn lines and is deliberately not the trash can (trash = delete). The
// class="ic-…" hooks are inert here; config/iconMotion.json moves them via iconHover.css.
export const ICONS = ICONS_DATA;

// A skin's own art, name → inner markup on a 16-grid with the colour baked in (ui/webcore/icons.js).
// While one is installed every glyph assembled afterwards is born in it; null = the line-art.
let skinArt = null;
export const setIconSkin = (table) => { skinArt = table || null; };
export const iconSkin = () => skinArt;

const skinFace = (name, cls, size) =>
  `<svg class="ic ic-${name}${cls}" viewBox="0 0 16 16" width="${size}" height="${size}" ` +
  `shape-rendering="crispEdges" aria-hidden="true" focusable="false">${skinArt[name]}</svg>`;

// The draw-mode toggle's two faces: complete <svg> strings on a 16-grid, not built by icon().
export const DRAW_MODE_ICON = {
  get line() { return skinArt?.['draw-mode-line'] ? skinFace('draw-mode-line', ' draw-mode-icon', 13) : SVG_ART.drawMode.line; },
  get rect() { return skinArt?.['draw-mode-rect'] ? skinFace('draw-mode-rect', ' draw-mode-icon', 13) : SVG_ART.drawMode.rect; },
};

// Returns '' for an unknown name, so a typo degrades to no glyph during markup assembly.
export function icon(name, { size = 16, cls = '', sw = 2 } = {}) {
  const extra = cls ? ` ${cls}` : '';
  if (skinArt?.[name]) return skinFace(name, extra, size);
  const inner = ICONS[name];
  if (!inner) return '';
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

// One clockwise turn of a glyph on click (iconMotion.json `extras.swap-click-turn`;
// css/animations/iconClick.css). Re-rendering mid-turn drops it, so callers spin AFTER repaint.
export const spinIconOnce = (btn) => {
  if (!btn || btn.classList.contains('icm-spun')) return;
  btn.classList.add('icm-spun');
  const done = () => btn.classList.remove('icm-spun');
  btn.addEventListener('animationend', done, { once: true });
  setTimeout(done, 800);   // a glyph that never animates (reduced motion) still clears
};
