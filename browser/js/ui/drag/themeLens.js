// Dragging the theme switch opens a lens: a circle on the pointer previewing the page in the other
// theme, built once and moved by its clip alone. A preview only: wherever the drag ends, or on
// Escape, the lens closes and the theme stays; a plain click on the switch still toggles it.
// Desktop twin: desktop/src/app/theme/ThemeLens.{hpp,cpp}, opened by ThemePainterLens.cpp.
import { wireIconDrag } from './iconDrag.js';
import { NO_COPY_ATTR, buildPageCopy, cutInheritance, pageSheets, readPageCss } from '../accent/themeCopy.js';
import { webcoreActive } from '../webcore/toggle.js';
import { THEME_INSTANT_CLASS, THEME_SWAP_CLASS } from '../motion.js';

export const LENS_RADIUS_PX = 90;
export const LENS_CLASS = 'theme-lens';
export const RIM_CLASS = 'theme-lens-rim';

export const otherTheme = (theme) => (theme === 'dark' ? 'light' : 'dark');
export const lensClip = (x, y, r = LENS_RADIUS_PX) => `circle(${r}px at ${x}px ${y}px)`;
export const rimTransform = (x, y, r = LENS_RADIUS_PX) => `translate(${x - r}px, ${y - r}px)`;

let current = null;

// The page copy in `theme`, seen through a circle at (x, y); a lens still up closes first.
export const openThemeLens = (x, y, { theme, isPicture, doc = globalThis.document } = {}) => {
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
  const lens = {
    host,
    rim,
    move(px, py) {
      host.style.clipPath = lensClip(px, py);
      rim.style.transform = rimTransform(px, py);
    },
    close() {
      host.remove();
      rim.remove();
      if (current === lens) current = null;
    },
  };
  lens.move(x, y);
  doc.body.append(host, rim);
  page.settle();
  current = lens;
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
  const shut = () => { lens?.close(); lens = null; };
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
    drop: shut,
    cancel: shut,
  };
};

// The sheets as written are read on the way to the switch, so the first lens has them.
export const wireThemeLens = (toggle, app) => {
  if (!toggle || !app) return null;
  toggle.addEventListener('pointerenter', () => { readPageCss(); });
  return wireIconDrag(toggle, { ...themeLensHooks(app), ghostCentred: true });   // the switch sits in the lens's middle
};
