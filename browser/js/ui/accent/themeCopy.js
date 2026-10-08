// The page as a given theme paints it: a still copy of the body for a shadow tree, under the page's
// own sheets re-rooted so the copy's root takes `data-theme` as <html> does. In a shadow tree its
// ids, radios and regions answer no query of the page; every canvas carries its pixels, the
// picture's inverted (it has no theme). Desktop twin: ThemePainter::otherThemeShot in
// desktop/src/app/theme/ThemePainterLens.cpp.
import { icon } from '../icons.js';
import { COPY_ATTR } from '../base.js';
import { guardedFetch } from '../../net/fetchGuard.js';

export const COPY_CLASS = 'theme-copy';
export const PICTURE_FILTER = 'invert(1)';
// A copy is seen through a small lens: a larger canvas is copied down to this many pixels.
export const MAX_COPY_PX = 4096 * 4096;
// An element no copy takes: whatever the copy is shown through.
export const NO_COPY_ATTR = 'data-no-copy';
const DROPPED = new Set(['SCRIPT', 'NOSCRIPT', 'TEMPLATE', 'IFRAME', 'OBJECT', 'EMBED', 'SOURCE', 'TRACK']);
// Never in a copy, at any depth: the drag's own (its ghost, a lens), a tooltip (the pointer's,
// which the drag has taken) and motion — a dust cloud's layer, and every canvas but the picture's.
export const UNCOPIED = '[data-drag-ghost], [role="tooltip"], .tooltip, .chat-status-tip, .disintegrate-host';
const skipped = (el, isPicture) => DROPPED.has(String(el.tagName).toUpperCase()) || el.hasAttribute?.(NO_COPY_ATTR)
  || !!el.matches?.(UNCOPIED) || (String(el.tagName).toUpperCase() === 'CANVAS' && !isPicture(el));
// What a copy keeps of a running animation: where things stand and whether they show, never a
// colour, which is the copy's theme to give.
const FROZEN = /^(transform|translate|rotate|scale|opacity|visibility|clipPath|display|width|height|(min|max)(Width|Height)|top|left|right|bottom)$/;
const KEYFRAME_KEYS = new Set(['offset', 'computedOffset', 'easing', 'composite']);

// `:root` matches nothing inside a shadow tree: the copy's root answers it instead.
export const rerootCss = (text) => text.replace(/:root\b/g, `.${COPY_CLASS}`);

const rulesOf = (sheet) => { try { return sheet.cssRules; } catch { return null; } };
let cache = null;   // { from: [sheet, ruleCount, written][], sheets }

// A linked sheet as written, read once: the CSSOM serializes a var() shorthand that a later
// longhand of its rule overrides (`border: … var(--x); border-bottom-width: 2px`) as nothing.
const written = new Map();   // href → its text, null while it is read
export const readPageCss = (doc = globalThis.document) => Promise.all([...doc.styleSheets].map(async (s) => {
  if (!s.href || written.has(s.href)) return;
  written.set(s.href, null);
  try {
    const res = await guardedFetch(s.href);
    if (!res.ok) throw new Error(`${res.status}`);
    written.set(s.href, await res.text());
  } catch { written.delete(s.href); }
}));
const textOf = (sheet, rules) => (sheet.href ? written.get(sheet.href) : sheet.ownerNode?.textContent)
  || Array.from(rules, (r) => r.cssText).join('\n');

// The page's sheets as sheets a shadow tree can adopt; rebuilt only when the page's change.
export const pageSheets = (doc = globalThis.document) => {
  const from = [...doc.styleSheets].filter((s) => !s.disabled)
    .map((s) => [s, rulesOf(s)?.length ?? -1, !!(s.href && written.get(s.href))]);
  const same = cache?.from.length === from.length && cache.from.every((k, i) => k.every((v, j) => v === from[i][j]));
  if (same) return cache.sheets;
  const sheets = [];
  for (const [s] of from) {
    const rules = rulesOf(s);
    if (!rules) continue;
    const copy = new CSSStyleSheet({ media: s.media?.mediaText || '', ...(s.href ? { baseURL: s.href } : {}) });
    copy.replaceSync(rerootCss(textOf(s, rules)));
    sheets.push(copy);
  }
  cache = { from, sheets };
  return sheets;
};

// The page's root inherits nothing, and neither may the copy's: each custom property the page hands
// down is reset on the host, whose own rule (themeLens.css) resets every other with `all: initial`.
export const cutInheritance = (host, from) => {
  const cs = getComputedStyle(from);
  for (let i = 0; i < cs.length; i++) if (cs[i].startsWith('--')) host.style.setProperty(cs[i], 'initial');
};

// Each element's animated properties that the copy freezes at their present value.
export const animatedProps = (doc) => {
  const held = new Map();
  for (const a of doc.getAnimations?.() ?? []) {
    const fx = a.effect;
    if (!fx?.target || fx.pseudoElement) continue;
    const props = held.get(fx.target) ?? new Set();
    for (const frame of fx.getKeyframes?.() ?? []) {
      for (const p of Object.keys(frame)) if (!KEYFRAME_KEYS.has(p) && FROZEN.test(p)) props.add(p);
    }
    held.set(fx.target, props);
  }
  return held;
};

export const copyPixels = (src, copy, picture = false) => {
  const { width: w, height: h } = src;
  if (!(w > 0 && h > 0)) return;
  const k = Math.min(1, Math.sqrt(MAX_COPY_PX / (w * h)));
  if (k < 1) {
    const cs = getComputedStyle(src);
    Object.assign(copy.style, { width: cs.width, height: cs.height });
  }
  copy.width = Math.max(1, Math.round(w * k));
  copy.height = Math.max(1, Math.round(h * k));
  try { copy.getContext('2d')?.drawImage(src, 0, 0, copy.width, copy.height); } catch { /* unreadable: blank */ }
  if (picture) copy.style.filter = PICTURE_FILTER;
};

// One element and its copy, in step: nothing in the copy may act (handlers, autofocus, a media or
// frame load); what needs the copy laid out, or would reshape the walk, is noted in `plan`. A
// region or window not shown (most of the page's markup) is kept as an empty box, so siblings match.
const SHALLOW_DEPTH = 2;
const visit = (src, copy, plan, depth = 0) => {
  for (const name of copy.getAttributeNames?.() ?? []) {
    if (/^on/i.test(name) || name === 'autofocus') copy.removeAttribute(name);
  }
  if (skipped(src, plan.isPicture)) { plan.drops.push(copy); return; }
  if (depth <= SHALLOW_DEPTH && getComputedStyle(src).display === 'none') { plan.emptied.push(copy); return; }
  const tag = String(src.tagName).toUpperCase();
  if (tag === 'CANVAS') copyPixels(src, copy, true);
  else if (tag === 'VIDEO' || tag === 'AUDIO') {
    copy.removeAttribute('src');
    copy.removeAttribute('autoplay');
    copy.setAttribute('preload', 'none');
  }
  if (src.id === 'theme-toggle') plan.glyphs.push(copy);
  if (src.scrollTop || src.scrollLeft) plan.scrolls.push([copy, src.scrollTop, src.scrollLeft]);
  const props = plan.held.get(src);
  if (props?.size) {
    const cs = getComputedStyle(src);
    for (const p of props) copy.style[p] = cs[p];
  }
  const a = src.children ?? [], b = copy.children ?? [];
  for (let i = 0; i < a.length && i < b.length; i++) visit(a[i], b[i], plan, depth + 1);
};

const copyAttributes = (from, to) => {
  for (const name of from.getAttributeNames?.() ?? []) to.setAttribute(name, from.getAttribute(name));
};

// The detached root of the copy in `theme`; `settle()` restores the scroll offsets once connected.
export const buildPageCopy = (doc, { theme, isPicture = () => false }) => {
  const html = doc.createElement('html');
  copyAttributes(doc.documentElement, html);
  html.setAttribute('data-theme', theme);
  html.setAttribute(COPY_ATTR, '');
  html.classList.add(COPY_CLASS);
  const view = doc.defaultView;
  if (view?.scrollX || view?.scrollY) {
    Object.assign(html.style, { position: 'relative', left: `${-view.scrollX}px`, top: `${-view.scrollY}px` });
  }
  const body = doc.createElement('body');
  copyAttributes(doc.body, body);
  html.appendChild(body);
  const plan = { drops: [], emptied: [], glyphs: [], scrolls: [], held: animatedProps(doc), isPicture };
  for (const child of [...doc.body.children]) {
    if (skipped(child, isPicture)) continue;
    const copy = child.cloneNode(true);
    visit(child, copy, plan);
    body.appendChild(copy);
  }
  for (const node of plan.drops) node.remove();
  for (const node of plan.emptied) node.replaceChildren();
  for (const node of plan.glyphs) node.innerHTML = icon(theme === 'dark' ? 'sun' : 'moon');
  const settle = () => {
    for (const [node, top, left] of plan.scrolls) { node.scrollTop = top; node.scrollLeft = left; }
  };
  return { html, settle };
};
