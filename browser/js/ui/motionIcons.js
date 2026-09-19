// ── The motion modes' icons ───────────────────────────────────────────────
// One inline-SVG glyph per interface-motion mode (motionPrefs.js MOTION_MODES) for the Visuals
// dropdown. Their parts carry the classes the row's hover animates (css/animations/motionModes.css).
// Byte-pinned to browser-extension/src/lib/motionIcons.js; the desktop paints the same shapes
// with QPainter (support/motionIcons.hpp).

const svg = (mode, body) =>
  `<svg class="mm-icon mm-${mode}" viewBox="0 0 16 16" width="16" height="16" aria-hidden="true" ` +
  'fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round">' +
  body + '</svg>';

// The line's length (the diagonal between the two points), the dash the hover redraws.
export const NONE_LINE_LEN = 11.4;
// …and the arrow's shaft, drawn from its bottom-left end on hover.
export const SLIDE_SHAFT_LEN = 9.9;

export const MOTION_ICONS = Object.freeze({
  none: svg('none',
    '<circle cx="8" cy="8" r="5.6"/>' +
    `<line class="mm-line" x1="12.05" y1="3.95" x2="3.95" y2="12.05" stroke-dasharray="${NONE_LINE_LEN}"/>`),
  slide: svg('slide',
    `<path class="mm-shaft" d="M4.5 11.5 L11.5 4.5" stroke-dasharray="${SLIDE_SHAFT_LEN}"/>` +
    '<path class="mm-head" d="M6.5 4.5 H11.5 V9.5"/>'),
  water: svg('water',
    '<path class="mm-drop" fill="currentColor" stroke="none" ' +
    'd="M8 2.4 C8 2.4 3.9 7.4 3.9 10.1 A4.1 4.1 0 0 0 12.1 10.1 C12.1 7.4 8 2.4 8 2.4 Z"/>'),
  fire: svg('fire',
    '<path class="mm-flame" fill="currentColor" stroke="none" ' +
    'd="M8.2 1.6 C9.4 4.2 12.3 5.8 12.3 9.3 A4.3 4.3 0 0 1 3.7 9.3 C3.7 7.4 5 6.4 5.6 4.9 ' +
    'C6.4 6 6.9 6.7 7.4 6.5 C7 5 7.5 3.2 8.2 1.6 Z"/>'),
  particles: svg('particles',
    '<g fill="currentColor" stroke="none">' +
    '<circle class="mm-mote" cx="4" cy="6" r="1.3"/><circle class="mm-mote" cx="8.5" cy="4.2" r="1"/>' +
    '<circle class="mm-mote" cx="12" cy="7" r="1.2"/><circle class="mm-mote" cx="6.2" cy="10.5" r="1.1"/>' +
    '<circle class="mm-mote" cx="10.4" cy="11.6" r="1.35"/></g>'),
});

// The icon for a mode, or '' for a value that has none (a stale stored key).
export const motionModeIcon = (mode) => MOTION_ICONS[mode] || '';
