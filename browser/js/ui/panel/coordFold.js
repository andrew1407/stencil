// The points panel's fold. Under particles the panel IS its dust, as the desktop's is
// (DockChrome::panelSurfaceFlight): its card is not painted, its header and table hold their open
// boxes as the picture the grains leave or form over, and the layout slides beneath on the one slide
// both apps read from config/motion.json (animations/collapse.css; desktop setPanelShown).
import { FOLD_INSTANT_CLASS, foldBox, foldDust, motionReduced, sweepDust } from '../motion.js';
import { TUNE } from '../motion/tune.js';

const COLLAPSED = 'coord-collapsed';
const FOLDING = 'coord-folding';
export const VEILED_CLASS = 'coord-veiled';
// The grains' flight, the layout's slide (under the veil or not) and the content fade; closing is no
// slower than opening (user report). The desktop reads the same keys (app/chrome/PanelSlide.hpp).
export const PANEL_DUST_IN_MS = TUNE.PANEL_DUST_IN_MS;
export const PANEL_DUST_OUT_MS = TUNE.PANEL_DUST_OUT_MS;
export const PANEL_SLIDE_IN_MS = TUNE.PANEL_SLIDE_IN_MS;
export const PANEL_SLIDE_OUT_MS = TUNE.PANEL_SLIDE_OUT_MS;
export const PANEL_FADE_MS = TUNE.PANEL_FADE_MS;
// The CSS each clock drives (animations/collapse.css, layout/coord/panel.css), set on the panel.
export const PANEL_CLOCK_VARS = Object.freeze({
  '--panel-slide-ms': PANEL_SLIDE_IN_MS, '--panel-slide-out-ms': PANEL_SLIDE_OUT_MS,
  '--panel-fade-ms': PANEL_FADE_MS,
});
// animations/responsive.css: below this the panel sits under the canvas and never slides.
const STACKED = '(max-width: 960px)';

const span = (a, b) => {
  const left = Math.min(a.left, b.left);
  const top = Math.min(a.top, b.top);
  return { left, top, width: Math.max(a.left + a.width, b.left + b.width) - left,
           height: Math.max(a.top + a.height, b.top + b.height) - top };
};
const pin = (el, box) => {
  for (const k of ['left', 'top', 'width', 'height']) el.style.setProperty(`--pin-${k}`, `${box[k]}px`);
};

// Returns `fold(hidden)`, which plays the panel into (or out of) its rail.
export function createCoordFold(panel, header, body) {
  for (const [name, ms] of Object.entries(PANEL_CLOCK_VARS)) panel.style.setProperty(name, `${ms}ms`);
  let timer = 0;
  // One class off, flushed, then the other, so the table's `display` never changes on the way: a
  // collapsed table dropped last kept it in the rail for its allow-discrete fade, and an open
  // one held out last came back through its @starting-style fade, a beat of empty panel.
  const settle = (hidden) => {
    const [first, then] = hidden ? [VEILED_CLASS, FOLDING] : [FOLDING, VEILED_CLASS];
    panel.classList.remove(first);
    void getComputedStyle(body).display;
    panel.classList.remove(then);
  };
  return (hidden) => {
    clearTimeout(timer);
    panel.classList.remove(VEILED_CLASS, FOLDING);
    if (hidden) sweepDust(panel);
    // Stacked, nothing slides: the table stays where it is as its own picture, unveiled.
    const wide = !globalThis.matchMedia?.(STACKED).matches;
    const still = motionReduced();
    const head = still || !wide ? null : foldBox(header, panel, COLLAPSED, false, FOLD_INSTANT_CLASS);
    const table = still || !wide ? null : foldBox(body, panel, COLLAPSED, false, FOLD_INSTANT_CLASS);
    const flew = foldDust(wide ? panel : body, panel, COLLAPSED, hidden, 'right', {
      box: head && table && span(head, table), inMs: PANEL_DUST_IN_MS, outMs: PANEL_DUST_OUT_MS,
      picture: true, edge: true, toggle: () => panel.classList.toggle(COLLAPSED, hidden),
    });
    panel.classList.add(FOLDING);
    if (flew && head && table) {
      pin(header, head);
      pin(body, table);
      panel.classList.add(VEILED_CLASS);
      // Closing, the rail is back as the last grains go; opening, the card as the slide lands.
      timer = setTimeout(settle, hidden ? PANEL_DUST_OUT_MS : PANEL_SLIDE_IN_MS, hidden);
      return;
    }
    // Hold the table out of the layout for half the slide (.coord-folding,
    // animations/collapse.css), by when the out-sine ease is ~70% done.
    const slide = hidden ? PANEL_SLIDE_OUT_MS : PANEL_SLIDE_IN_MS;
    timer = setTimeout(() => panel.classList.remove(FOLDING), still ? 0 : slide / 2);
  };
}
