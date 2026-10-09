// Dragging the theme switch opens a lens: a circle on the pointer previewing the page in the other
// theme, built once and moved by its clip alone. A preview only: released anywhere, or on Escape,
// the lens closes and the theme stays; a plain click is still the switch's own.
// Desktop twin: desktop/src/app/theme/ThemeLens.{hpp,cpp}, opened by ThemePainterLens.cpp.
import { wireIconDrag } from './iconDrag.js';
import { NO_COPY_ATTR, buildPageCopy, cutInheritance, pageSheets, readPageCss } from '../accent/themeCopy.js';
import { webcoreActive } from '../webcore/toggle.js';
import { THEME_INSTANT_CLASS, THEME_SWAP_CLASS } from '../motion.js';
import { motionReduced } from '../motion/motionPrefs.js';

export const LENS_RADIUS_PX = 90;
// ms the circle takes to open from a point to its full radius, and to close back into one
// (desktop ThemeLens::GROW_MS / CLOSE_MS).
export const LENS_GROW_MS = 192;
export const LENS_CLOSE_MS = 360;
export const LENS_CLASS = 'theme-lens';
export const RIM_CLASS = 'theme-lens-rim';

export const otherTheme = (theme) => (theme === 'dark' ? 'light' : 'dark');
export const lensClip = (x, y, r = LENS_RADIUS_PX) => `circle(${r}px at ${x}px ${y}px)`;
// The rim is drawn at the full radius and scaled about its centre to `r`.
export const rimTransform = (x, y, r = LENS_RADIUS_PX) =>
  `translate(${x - LENS_RADIUS_PX}px, ${y - LENS_RADIUS_PX}px) scale(${r / LENS_RADIUS_PX})`;
// The radius `ms` after the lens opened: an ease-out (cubic) from 0, full at LENS_GROW_MS.
export const lensRadiusAt = (ms) =>
  (ms >= LENS_GROW_MS ? LENS_RADIUS_PX : LENS_RADIUS_PX * (1 - (1 - Math.max(0, ms) / LENS_GROW_MS) ** 3));
// Closing from `from` px: the same ease, run back down to 0 over LENS_CLOSE_MS.
export const closingRadiusAt = (ms, from = LENS_RADIUS_PX) =>
  from * (1 - lensRadiusAt(ms * LENS_GROW_MS / LENS_CLOSE_MS) / LENS_RADIUS_PX);

let current = null;

// The page copy in `theme`, seen through a circle at (x, y) that opens from a point and closes back
// into one, or shows and goes whole when `still`; a lens still up closes first.
export const openThemeLens = (x, y, { theme, isPicture, doc = globalThis.document, still = motionReduced(),
                                      raf = globalThis.requestAnimationFrame,
                                      now = () => globalThis.performance?.now?.() ?? Date.now() } = {}) => {
  current?.close();
  const host = doc.createElement('div');
  host.className = LENS_CLASS;
  host.setAttribute(NO_COPY_ATTR, '');
  host.setAttribute('aria-hidden', 'true');
  host.inert = true;
  cutInheritance(host, doc.body);
  const shadow = host.attachShadow({ mode: 'open' });
  shadow.adoptedStyleSheets = pageSheets(doc);
  const page = buildPageCopy(doc, { theme, isPicture });
  shadow.appendChild(page.html);
  const rim = doc.createElement('div');
  rim.className = RIM_CLASS;
  rim.setAttribute(NO_COPY_ATTR, '');
  rim.setAttribute('aria-hidden', 'true');
  rim.style.width = rim.style.height = `${2 * LENS_RADIUS_PX}px`;
  const grows = !still && typeof raf === 'function';
  let t0 = now();
  let at = { x, y }, r = grows ? 0 : LENS_RADIUS_PX, open = true;
  const paint = () => {
    host.style.clipPath = lensClip(at.x, at.y, r);
    rim.style.transform = rimTransform(at.x, at.y, r);
  };
  const grow = () => {
    if (!open) return;
    r = lensRadiusAt(now() - t0);
    paint();
    if (r < LENS_RADIUS_PX) raf(grow);
  };
  const lens = {
    host,
    rim,
    get radius() { return r; },
    move(px, py) {
      at = { x: px, y: py };
      paint();
    },
    // `done` runs once the circle has closed into its point (at once when nothing shrinks).
    close(done = null) {
      if (!open) return;
      open = false;
      if (current === lens) current = null;
      const gone = () => { host.remove(); rim.remove(); done?.(); };
      if (!grows || r <= 0) { gone(); return; }
      const from = r;
      t0 = now();
      const shrink = () => {
        r = closingRadiusAt(now() - t0, from);
        paint();
        if (r > 0) raf(shrink);
        else gone();
      };
      raf(shrink);
    },
  };
  paint();
  doc.body.append(host, rim);
  page.settle();
  current = lens;
  if (grows) raf(grow);
  return lens;
};

// No lens opens under the webcore skin (its face paints its own icon art) or over a swap still in
// flight, which the copy would carry; the drag goes on without one, and ends as one does.
const lensAllowed = (doc) => {
  const root = doc.documentElement;
  return !webcoreActive(doc) && !root?.classList?.contains(THEME_INSTANT_CLASS)
    && !root?.classList?.contains(THEME_SWAP_CLASS);
};

export const themeLensHooks = (app, { open = openThemeLens, doc = globalThis.document } = {}) => {
  let lens = null;
  const shut = (done = null) => {
    const closing = lens;
    lens = null;
    if (closing) closing.close(done);
    else done?.();
  };
  const isPicture = (canvas) => !!app.renderer?.layers?.().includes(canvas);
  return {
    start: ({ x, y }) => {
      shut();
      try {
        if (lensAllowed(doc)) lens = open(x, y, { theme: otherTheme(app.theme), isPicture, doc });
      } catch { lens = null; /* a view only: the drag goes on without it */ }
      return true;
    },
    move: ({ x, y }) => lens?.move(x, y),
    drop: () => shut(),
    cancel: () => shut(),
  };
};

// The sheets as written are read on the way to the switch, so the first lens has them.
export const wireThemeLens = (toggle, app) => {
  if (!toggle || !app) return null;
  toggle.addEventListener('pointerenter', () => { readPageCss(); });
  return wireIconDrag(toggle, { ...themeLensHooks(app), ghostCentred: true });   // the switch sits in the lens's middle
};
