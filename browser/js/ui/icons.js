// ── Shared inline-SVG icon set ──────────────────────────────────
// One source of truth for every glyph in the browser app (toolbar, context menu,
// modals, notifications). Stroked line-art on a 24×24 grid using `currentColor`, so
// glyphs inherit button/text color and theme (light/dark/accent) automatically.
// The glyph paths live in config/icons.json (name → inner SVG markup in a
// 0 0 24 24 viewBox); extension/src/lib/icons.js mirrors it — keep in sync.
// Pure strings (no DOM) so this leaf imports cleanly in Node for the markup tests;
// icon() output contains no backtick or "${" (the markup tests assert that).
import ICONS_DATA from '../config/icons.json' with { type: 'json' };

// Notes that used to sit on individual rows: 'eraser' wipes drawn lines and is
// deliberately NOT the trash can (trash = delete project/file everywhere else);
// 'more' is the overflow menu ("⋯"); 'sparkle' is the assistant chat-bubble mark.
export const ICONS = ICONS_DATA;

// Build an <svg> string for a named icon. `size` px (square), optional extra
// `cls`, optional stroke width `sw`. Returns '' for an unknown name so a typo
// degrades to no glyph rather than throwing during markup assembly.
export function icon(name, { size = 16, cls = '', sw = 2 } = {}) {
  const inner = ICONS[name];
  if (!inner) return '';
  const extra = cls ? ` ${cls}` : '';
  return `<svg class="ic ic-${name}${extra}" viewBox="0 0 24 24" width="${size}" height="${size}" ` +
    `fill="none" stroke="currentColor" stroke-width="${sw}" stroke-linecap="round" ` +
    `stroke-linejoin="round" aria-hidden="true" focusable="false">${inner}</svg>`;
}
