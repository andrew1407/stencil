// The aqua skin's live half, worn by lib/skin/install.js while <html data-skin="aqua"> is stamped:
// the gel and the mark in the accent on screen (Blue once the word has stored it), every glyph in the
// glossy art. Browser twins: js/ui/aqua/toggle.js, logo.js, icons.js.
import { AQUA_LOGO, GEL_NAMES, aquaIconArt, aquaLogoArt, gelTokens, hasAquaIcon } from './rules.js';
import { skinOn } from '../skin/switch.js';
import { GLYPHS, MARKS, glyphOf, glyphsAt } from '../skin/page.js';

export const isAquaOn = (doc = globalThis.document) => skinOn(doc) === 'aqua';

// The preset the word stores as the user's accent, as the browser's AQUA_ACCENT.
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

export const makeAquaSkin = (doc) => {
  const root = doc.documentElement;
  return {
    wear: (look) => { paintGel(root, look); swapAquaIcons(doc.body, true, look); },
    tint: (look) => { paintGel(root, look); swapAquaIcons(doc.body, true, look, MARKS); },
    shed: () => { for (const name of GEL_NAMES) root.style.removeProperty(name); swapAquaIcons(doc.body, false); },
    dress: (node, look) => swapAquaIcons(node, true, look),
  };
};
