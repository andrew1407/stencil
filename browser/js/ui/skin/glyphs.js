// The glyphs and the header mark as a session skin swaps them: every glyph redrawn in the art ui/icons.js
// serves now, in place, and the mark's own markup kept to put back. Extension twin: lib/skin/page.js.
import { icon, DRAW_MODE_ICON } from '../icons.js';

const NAME_RE = /^ic-([a-z0-9-]+)$/;
const SW_RE = /^ic-sw([\d.]+)$/;

// Redraw every glyph under `root` in whatever ui/icons.js is serving now, the draw-mode faces with
// them, in place (their listeners stay); `on` = a skin's art is being served.
export const swapGlyphs = (root, on) => {
  for (const svg of [...root.querySelectorAll('svg.ic')]) {
    if (svg.classList.contains('draw-mode-icon')) continue;   // not in the line-art table
    const classes = [...svg.classList];
    const name = classes.map((c) => NAME_RE.exec(c)?.[1]).find(Boolean);
    if (!name) continue;
    // The line-art's stroke width rides along as a class, so the way back keeps it.
    const stroke = svg.getAttribute('stroke-width');
    const kept = classes.filter((c) => c !== 'ic' && c !== `ic-${name}` && !SW_RE.test(c));
    if (on && stroke && stroke !== '2') kept.push(`ic-sw${stroke}`);
    const sw = classes.map((c) => SW_RE.exec(c)?.[1]).find(Boolean);
    svg.outerHTML = icon(name, { size: Number(svg.getAttribute('width')) || 16, cls: kept.join(' '), sw: sw ? Number(sw) : 2 });
  }
  for (const svg of [...root.querySelectorAll('svg.draw-mode-icon')]) {
    const isRect = svg.classList.contains('ic-draw-mode-rect') || /ic-box/.test(svg.innerHTML);
    svg.outerHTML = isRect ? DRAW_MODE_ICON.rect : DRAW_MODE_ICON.line;
  }
};

const marks = new WeakMap();   // the header mark → its own markup and viewBox, kept to put back

// The header mark under `root` dressed by `dress(logo)` (on; again to repaint), or put back (off).
export const swapLogoArt = (root, on, dress = null) => {
  const logo = root?.querySelector?.('svg.app-logo');
  if (!logo) return;
  if (on) {
    if (!marks.has(logo)) marks.set(logo, { inner: logo.innerHTML, viewBox: logo.getAttribute('viewBox') });
    dress?.(logo);
    return;
  }
  const was = marks.get(logo);
  if (!was) return;
  logo.innerHTML = was.inner;
  if (was.viewBox == null) logo.removeAttribute('viewBox'); else logo.setAttribute('viewBox', was.viewBox);
  logo.removeAttribute('shape-rendering');
  marks.delete(logo);
};
