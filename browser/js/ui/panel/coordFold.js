// The points panel's fold. Under particles the panel IS its dust, as the desktop's is
// (DockChrome::panelSurfaceFlight): its card is not painted, its header and table hold their open
// boxes as the picture the grains leave or form over, and the layout slides beneath on the one slide
// both apps read from config/motion.json (animations/collapse.css; desktop setPanelShown).
import { motionMs, motionReduced } from '../motion/motionPrefs.js';
import { sweepDust } from '../motion/surface/tiles.js';
import { FOLD_INSTANT_CLASS, FOLD_POURED, foldBox, foldDust } from '../motion/control/tips.js';
import { TUNE } from '../motion/tune.js';
import { genieEnabled } from '../motion/genie/window.js';
import { isStacked } from '../../utils.js';

const COLLAPSED = 'coord-collapsed';
const FOLDING = 'coord-folding';
export const VEILED_CLASS = 'coord-veiled';
// A fold spans the column to its bottom, the card drawn at its own height (animations/collapse.css).
export const TALL_CLASS = 'coord-tall';
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
// Past the longest fold clock, so no flight still measures or pictures the column when it goes (ms).
const TALL_HOLD_MS = Math.max(PANEL_DUST_IN_MS, PANEL_DUST_OUT_MS, PANEL_SLIDE_IN_MS, PANEL_SLIDE_OUT_MS) + 200;

// Sliding's home: a strip on the column's right edge level with the chevron, as the desktop's (DockChrome::pourPanel
// lands on the chevron past the panel's edge): the column folds sideways into it, never up through its own card.
const edgeHome = (panel, toggle) => () => {
  const p = panel.getBoundingClientRect();
  const t = (toggle ?? panel).getBoundingClientRect();
  return { left: p.left + p.width, top: t.top, width: 2, height: Math.max(2, t.height) };
};

// Stacked, the list folds its height on the slide's clock while .coord-folding keeps it displayed and clipped
// (animations/responsive.css); folded, it holds an inline 0: the late display flip would show it whole for a beat.
const foldEase = () => globalThis.getComputedStyle?.(document.documentElement).getPropertyValue('--fold-ease').trim() || 'ease-out';
const foldHeight = (body, panel, hidden, ms, running) => {
  // Opened, the list stands below the fold as often as not: the page follows it (user report).
  const show = () => { if (!hidden) try { panel.scrollIntoView?.({ block: 'nearest', behavior: ms ? 'smooth' : 'instant' }); } catch { /* decoration */ } };
  const midway = running?.playState === 'running' ? `${body.getBoundingClientRect().height}px` : null;
  running?.cancel();
  body.style.height = '';
  const open = `${body.offsetHeight || 0}px`;
  const [from, to] = [midway ?? (hidden ? open : '0px'), hidden ? '0px' : open];
  if (!ms || open === '0px' || typeof body.animate !== 'function') { body.style.height = hidden ? '0px' : ''; show(); return null; }
  body.style.height = from;   // the flight's first frame is the next one: pinned till then
  const f = body.animate([{ height: from }, { height: to }], { duration: ms, easing: foldEase() });
  f.finished.then(() => { body.style.height = hidden ? '0px' : ''; show(); }, () => {});
  return f;
};

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
  let tallTimer = 0;
  let lift = null;   // the stacked list's height run
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
    clearTimeout(tallTimer);
    panel.classList.remove(VEILED_CLASS, FOLDING, TALL_CLASS);
    if (hidden) sweepDust(panel);
    // Stacked, nothing slides: the table stays where it is as its own picture, unveiled.
    const wide = !isStacked();
    const still = motionReduced();
    const head = still || !wide ? null : foldBox(header, panel, COLLAPSED, false, FOLD_INSTANT_CLASS);
    const table = still || !wide ? null : foldBox(body, panel, COLLAPSED, false, FOLD_INSTANT_CLASS);
    const rest = head && table ? foldBox(panel, panel, COLLAPSED, false, FOLD_INSTANT_CLASS) : null;
    const restCard = rest ? `${rest.height}px` : '';
    if (rest) {
      // Sliding's picture is the whole column as it stands, card and the page under it, as the desktop's dock
      // pours (taken as the fold starts); on screen the column below the card stays unpainted.
      panel.style.setProperty('--coord-card-h', restCard);
      panel.style.setProperty('--coord-tall-fill', genieEnabled() ? 'var(--bg-container)' : 'transparent');
      panel.classList.add(TALL_CLASS);
      tallTimer = setTimeout(() => panel.classList.remove(TALL_CLASS), motionMs(TALL_HOLD_MS));
    }
    const card = rest ? foldBox(panel, panel, COLLAPSED, false, FOLD_INSTANT_CLASS) : null;
    const flew = foldDust(wide ? panel : body, panel, COLLAPSED, hidden, 'right', {
      box: head && table && (card ? span(card, span(head, table)) : span(head, table)), inMs: PANEL_DUST_IN_MS, outMs: PANEL_DUST_OUT_MS,
      picture: true, edge: true, toggle: () => panel.classList.toggle(COLLAPSED, hidden),
      // Stacked, nothing pours sideways: Sliding folds the list's height under its fade like the other modes.
      home: wide ? edgeHome(panel, panel.querySelector('#toggle-coord-panel') ?? header) : null,
      pourMs: hidden ? PANEL_SLIDE_OUT_MS : PANEL_SLIDE_IN_MS,
    });
    if (rest) panel.style.setProperty('--coord-tall-fill', 'transparent');
    if (flew === FOLD_POURED) return;   // Sliding: hidden under its picture, the width slides on its own
    panel.classList.add(FOLDING);
    if (flew && head && table) {
      pin(header, head);
      pin(body, table);
      panel.classList.add(VEILED_CLASS);
      // Closing, the rail is back as the last grains go; opening, the card as the slide lands.
      timer = setTimeout(settle, motionMs(hidden ? PANEL_DUST_OUT_MS : PANEL_SLIDE_IN_MS), hidden);
      return;
    }
    // Wide, the table is held out of the layout for half the slide (.coord-folding, animations/collapse.css),
    // by when the out-sine ease is ~70% done; stacked, the list folds its height over the whole of it.
    const slide = hidden ? PANEL_SLIDE_OUT_MS : PANEL_SLIDE_IN_MS;
    if (!wide) lift = foldHeight(body, panel, hidden, still ? 0 : motionMs(slide), lift);
    timer = setTimeout(() => panel.classList.remove(FOLDING), still ? 0 : motionMs(wide ? slide / 2 : slide));
  };
}
