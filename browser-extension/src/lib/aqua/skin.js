// The aqua skin's live half, worn by lib/skin/install.js while <html data-skin="aqua"> is stamped:
// the page in the Blue preset for the session (never the store), the gel and the mark in the accent
// on screen, every glyph in the glossy art. Browser twins: js/ui/aqua/toggle.js, logo.js, icons.js.
import { AQUA_LOGO, GEL_NAMES, aquaIconArt, aquaLogoArt, gelTokens, hasAquaIcon } from './rules.js';
import { accentHex } from '../logo/accents.js';
import { needsDarkGlyph } from '../accent/ink.js';
import { skinOn } from '../skin/switch.js';
import { GLYPHS, MARKS, glyphOf, glyphsAt } from '../skin/page.js';

export const isAquaOn = (doc = globalThis.document) => skinOn(doc) === 'aqua';

// The preset the skin dresses the page in, as the browser's AQUA_ACCENT.
export const AQUA_ACCENT = 'blue';

// The line-art's own paint, set aside so the art's fills and strokes are the only ones.
const KEPT_ATTRS = Object.freeze(['viewBox', 'fill', 'stroke', 'stroke-width', 'stroke-linecap', 'stroke-linejoin', 'src']);
const kept = new WeakMap();   // element → what it wore before the skin

const wear = (el, name, look) => {
  const isLogo = name === 'logo';
  if (kept.has(el) && !isLogo) return;
  if (!kept.has(el)) kept.set(el, { inner: el.innerHTML, attrs: KEPT_ATTRS.map((a) => [a, el.getAttribute(a)]) });
  if (el.tagName.toLowerCase() === 'img') {
    el.setAttribute('src', `data:image/svg+xml,${encodeURIComponent(aquaLogoArt(AQUA_LOGO.svg, look.accent, look.dark))}`);
  } else {
    for (const a of KEPT_ATTRS) el.removeAttribute(a);
    el.setAttribute('viewBox', isLogo ? '0 0 64 64' : '0 0 32 32');
    el.innerHTML = isLogo ? aquaLogoArt(AQUA_LOGO.inner, look.accent, look.dark) : aquaIconArt(name);
  }
  el.setAttribute('data-aq', '');
};

const restore = (el) => {
  const was = kept.get(el);
  if (!was) return;
  if (el.tagName.toLowerCase() !== 'img') el.innerHTML = was.inner;
  for (const [a, v] of was.attrs) if (v === null) el.removeAttribute(a); else el.setAttribute(a, v);
  el.removeAttribute('data-aq');
  kept.delete(el);
};

// The glyphs `selector` names at or under `root`, in the glossy art (`on`) or as they were.
export const swapAquaIcons = (root, on, look = {}, selector = GLYPHS) => {
  for (const el of glyphsAt(root, selector)) {
    const name = glyphOf(el, hasAquaIcon);
    if (!name) continue;
    if (on) wear(el, name, look); else restore(el);
  }
};

const paintGel = (root, look) => {
  for (const [name, value] of Object.entries(gelTokens(look.accent, look.dark))) root.style.setProperty(name, value);
};

// Paints an accent as lib/accent/accent.js's apply does, minus the store and its mirror.
const showAccent = (root, key, custom) => {
  if (custom) root.style.setProperty('--accent', custom); else root.style.removeProperty('--accent');
  if (key) root.setAttribute('data-accent', key); else root.removeAttribute('data-accent');
  if (needsDarkGlyph(custom || accentHex(key))) root.setAttribute('data-accent-light', '');
  else root.removeAttribute('data-accent-light');
};

// Blue while the skin is on; the page's own accent back after, unless the user picked one meanwhile.
const makeBlueWearer = (root, win) => {
  let own = null;
  const drop = () => { own = null; };
  const key = win.StencilAccent?.storageKey ?? 'stencil_accent';
  const onStorage = (e) => { if (e.key === key || e.key === null) drop(); };
  win.addEventListener?.('stencil:accent-changed', drop);
  win.addEventListener?.('storage', onStorage);
  return {
    wear: () => {
      own = { key: root.getAttribute('data-accent'), custom: root.style.getPropertyValue('--accent') };
      if (own.custom || own.key !== AQUA_ACCENT) showAccent(root, AQUA_ACCENT, '');
    },
    giveBack: () => {
      if (own && (own.custom || own.key !== AQUA_ACCENT)) showAccent(root, own.key, own.custom);
      own = null;
    },
    dispose: () => {
      win.removeEventListener?.('stencil:accent-changed', drop);
      win.removeEventListener?.('storage', onStorage);
    },
  };
};

export const makeAquaSkin = (doc, win = globalThis) => {
  const root = doc.documentElement;
  const blue = makeBlueWearer(root, win);
  return {
    enter: blue.wear,
    wear: (look) => { paintGel(root, look); swapAquaIcons(doc.body, true, look); },
    tint: (look) => { paintGel(root, look); swapAquaIcons(doc.body, true, look, MARKS); },
    shed: () => { for (const name of GEL_NAMES) root.style.removeProperty(name); swapAquaIcons(doc.body, false); },
    leave: blue.giveBack,
    dress: (node, look) => swapAquaIcons(node, true, look),
    dispose: blue.dispose,
  };
};
