// A page the theme lens can copy (js/ui/drag/themeLens*.js): stub elements that clone deep the way
// the DOM does (attributes and children, never scroll offsets or canvas pixels), canvases that
// record what is drawn into them, sheets a stand-in CSSStyleSheet adopts, and a shadow root.
import { createStubElement, createStubDocument } from './dom.js';

// Enough of `matches` for a list of `.class`, `[attr]` and `[attr="value"]` selectors.
const matchesOne = (el, sel) => {
  const cls = /^\.([\w-]+)$/.exec(sel);
  if (cls) return el.classes.has(cls[1]) || String(el.attrs.get('class') ?? '').split(/\s+/).includes(cls[1]);
  const attr = /^\[([\w-]+)(?:="([^"]*)")?\]$/.exec(sel);
  return !!attr && el.attrs.has(attr[1]) && (attr[2] === undefined || el.attrs.get(attr[1]) === attr[2]);
};

const shape = (el) => {
  el.matches = (list) => list.split(',').some((sel) => matchesOne(el, sel.trim()));
  el.getAttributeNames = () => [...el.attrs.keys()];
  el.replaceChildren = () => { el.children.length = 0; };
  el.scrollTop = el.scrollTop ?? 0;
  el.scrollLeft = el.scrollLeft ?? 0;
  el.cloneNode = () => clone(el);
  el.attachShadow = (init) => {
    el.shadowInit = init;
    el.shadowRoot = createStubElement('#shadow-root', { adoptedStyleSheets: [] });
    return el.shadowRoot;
  };
  if (el.tagName === 'CANVAS') {
    el.drawn = [];
    el.getContext = () => ({ drawImage: (...args) => el.drawn.push(args) });
  }
  return el;
};

export const node = (tag, { id = '', attrs = {}, kids = [], scroll = [0, 0], size = null } = {}) => {
  const el = shape(createStubElement(tag, { id }));
  if (id) el.setAttribute('id', id);
  for (const [k, v] of Object.entries(attrs)) el.setAttribute(k, v);
  [el.scrollTop, el.scrollLeft] = scroll;
  if (size) [el.width, el.height] = size;
  for (const kid of kids) el.appendChild(kid);
  return el;
};

function clone(el) {
  const copy = shape(createStubElement(el.tagName.toLowerCase(), { id: el.id }));
  for (const [k, v] of el.attrs) copy.setAttribute(k, v);
  for (const c of el.classes) copy.classList.add(c);
  if (el.tagName === 'CANVAS') [copy.width, copy.height] = [el.width, el.height];
  for (const kid of el.children) copy.appendChild(clone(kid));
  return copy;
}

// What a test reads back off the copy: the nodes in tree order, and one by its id.
export const walk = (el, out = []) => {
  out.push(el);
  for (const kid of el.children) walk(kid, out);
  return out;
};
export const byId = (root, id) => walk(root).find((n) => n.id === id) ?? null;

// `new CSSStyleSheet(opts)` with replaceSync, recording what it was handed.
export class SheetStandIn {
  constructor(opts = {}) { this.opts = opts; this.text = null; }
  replaceSync(text) { this.text = text; }
}

// A document over `kids` (the body's children); `html` and `body` take attributes, `sheets` is
// document.styleSheets and `animations` what getAnimations() reports.
export const pageDoc = ({ kids = [], html = {}, body = {}, sheets = [], animations = [] } = {}) => {
  const doc = createStubDocument({
    createElement: (tag) => node(tag),
    styleSheets: sheets,
    getAnimations: () => animations,
  });
  doc.documentElement = node('html', { attrs: html });
  doc.body = node('body', { attrs: body, kids });
  return doc;
};
