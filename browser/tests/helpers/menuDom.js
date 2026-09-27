// A small live tree the context menu (js/ui/contextMenu/, js/ui/ctx/) really wires on: its own
// markup parsed in (insertAdjacentHTML / innerHTML), parent links, focus, capture-aware listeners,
// and the selectors the menu speaks. installMenuDom() makes it the document until restore().

const matches = (el, sel) => {
  const nots = [];
  const c = sel.trim().replace(/:not\(([^()]*)\)/g, (_, inner) => { nots.push(inner); return ''; });
  if (nots.some((n) => matches(el, n))) return false;
  const m = /^([a-z][\w-]*)?(#[\w-]+)?((?:\.[\w-]+)*)((?:\[[\w-]+(?:="[^"]*")?\])*)$/.exec(c);
  if (!m) throw new Error(`menuDom: unsupported selector "${sel}"`);
  const [, tag, id, cls, attrs] = m;
  if (tag && el.tagName !== tag.toUpperCase()) return false;
  if (id && el.id !== id.slice(1)) return false;
  if (cls && !cls.slice(1).split('.').every((k) => el.classList.contains(k))) return false;
  return !attrs || [...attrs.matchAll(/\[([\w-]+)(?:="([^"]*)")?\]/g)]
    .every(([, a, v]) => (v === undefined ? el.getAttribute(a) !== null : el.getAttribute(a) === v));
};
const matchesAny = (el, list) => list.split(',').some((sel) => matches(el, sel));
const within = (el, anc) => { for (let n = el.parentElement; n; n = n.parentElement) if (matches(n, anc)) return true; return false; };
export const descendants = (el, out = []) => { for (const c of el.children) { out.push(c); descendants(c, out); } return out; };
const select = (root, list) => {
  const found = new Set();
  for (const part of list.split(',').map((p) => p.trim())) {
    const scoped = /^:scope\s*>\s*(.+)$/.exec(part);
    const [anc, comp] = scoped ? [null, scoped[1]] : part.includes(' ') ? part.split(/\s+/) : [null, part];
    const pool = scoped ? root.children : descendants(root);
    for (const el of pool) if (matches(el, comp) && (!anc || within(el, anc))) found.add(el);
  }
  return descendants(root).filter((el) => found.has(el));
};

// What the stylesheet hides: display:none inline, [hidden], or a closed flyout (.ctx-sub).
const rendered = (el) => {
  for (let n = el; n; n = n.parentElement) {
    if (n.hidden || n.style.display === 'none') return false;
    if (n.classList.contains('ctx-sub') && !n.classList.contains('ctx-sub-visible')) return false;
  }
  return true;
};

const VOID = new Set(['input', 'img', 'br', 'hr', 'source', 'wbr']);
const TOKEN = /<!--[\s\S]*?-->|<\/([\w-]+)\s*>|<([\w-]+)((?:\s+[^\s=>/]+(?:\s*=\s*(?:"[^"]*"|'[^']*'|[^\s>]+))?)*)\s*(\/?)>|([^<]+)/g;
const ATTR = /([^\s=>/]+)(?:\s*=\s*(?:"([^"]*)"|'([^']*)'|([^\s>]+)))?/g;
const camel = (s) => s.replace(/-(\w)/g, (_, c) => c.toUpperCase());

export const installMenuDom = () => {
  const byId = new Map();
  const parse = (html) => {
    const root = make('template');
    const stack = [root];
    for (const [, close, open, attrs, selfClose, text] of String(html).matchAll(TOKEN)) {
      const top = stack.at(-1);
      if (close) {
        const i = stack.findLastIndex((n, k) => k > 0 && n.tagName === close.toUpperCase());
        if (i > 0) stack.length = i;
      } else if (open) {
        const el = make(open.toLowerCase());
        for (const [, name, a, b, c] of (attrs || '').matchAll(ATTR)) el.setAttribute(name, a ?? b ?? c ?? '');
        top.appendChild(el);
        if (!VOID.has(open.toLowerCase()) && !selfClose) stack.push(el);
      } else if (text) top.text += text;
    }
    return root.children.slice();
  };
  const make = (tag = 'div') => {
    const cls = new Set();
    const attr = new Map();
    const listeners = [];
    let html = '';
    const style = {
      setProperty(k, v) { style[k] = v; }, removeProperty(k) { delete style[k]; },
      getPropertyValue(k) { return style[k] ?? ''; },
    };
    const el = {
      tagName: tag.toUpperCase(), children: [], parentElement: null, nodeType: 1, text: '',
      style, dataset: {}, disabled: false, hidden: false, value: '', type: '', clicks: 0,
      get id() { return attr.get('id') ?? ''; },
      set id(v) { attr.set('id', v); byId.set(v, el); },
      get parentNode() { return el.parentElement; },
      get childNodes() { return el.children; },
      get className() { return [...cls].join(' '); },
      set className(v) { cls.clear(); String(v).split(/\s+/).filter(Boolean).forEach((k) => cls.add(k)); },
      get textContent() { return el.text + el.children.map((c) => c.textContent).join(''); },
      set textContent(v) { el.children.slice().forEach((c) => c.remove()); el.text = String(v); },
      get innerHTML() { return html; },
      set innerHTML(v) { el.textContent = ''; html = String(v); parse(v).forEach((c) => el.appendChild(c)); },
      classList: {
        add: (...ks) => ks.forEach((k) => cls.add(k)), remove: (...ks) => ks.forEach((k) => cls.delete(k)),
        contains: (k) => cls.has(k),
        toggle: (k, on = !cls.has(k)) => { if (on) cls.add(k); else cls.delete(k); return on; },
      },
      getAttribute: (k) => (k.startsWith('data-') ? el.dataset[camel(k.slice(5))] ?? null
        : attr.has(k) ? attr.get(k) : null),
      setAttribute: (k, v) => {
        if (k === 'id') { el.id = v; return; }
        attr.set(k, String(v));
        if (k === 'class') el.className = v;
        else if (k.startsWith('data-')) el.dataset[camel(k.slice(5))] = String(v);
        else if (['disabled', 'hidden'].includes(k)) el[k] = true;
        else if (['type', 'value', 'placeholder', 'name'].includes(k)) el[k] = String(v);
      },
      hasAttribute: (k) => attr.has(k),
      removeAttribute: (k) => attr.delete(k),
      appendChild: (c) => { c.parentElement?.removeChild(c); c.parentElement = el; el.children.push(c); return c; },
      append: (...cs) => cs.forEach((c) => el.appendChild(c)),
      prepend: (c) => { c.parentElement?.removeChild(c); c.parentElement = el; el.children.unshift(c); return c; },
      insertBefore: (c, ref) => {
        c.parentElement?.removeChild(c);
        c.parentElement = el;
        const i = ref ? el.children.indexOf(ref) : -1;
        el.children.splice(i < 0 ? el.children.length : i, 0, c);
        return c;
      },
      removeChild: (c) => { el.children.splice(el.children.indexOf(c), 1); c.parentElement = null; return c; },
      remove: () => el.parentElement?.removeChild(el),
      insertAdjacentHTML: (pos, markup) => {
        const nodes = parse(markup);
        if (pos === 'beforebegin') nodes.forEach((n) => el.parentElement.insertBefore(n, el));
        else if (pos === 'beforeend') nodes.forEach((n) => el.appendChild(n));
        else throw new Error(`menuDom: insertAdjacentHTML(${pos})`);
      },
      contains: (n) => n === el || descendants(el).includes(n),
      closest: (sel) => { for (let n = el; n; n = n.parentElement) if (matchesAny(n, sel)) return n; return null; },
      matches: (sel) => matchesAny(el, sel),
      querySelector: (sel) => select(el, sel)[0] || null,
      querySelectorAll: (sel) => select(el, sel),
      addEventListener: (type, fn, opt) => listeners.push({ type, fn, capture: opt === true || !!opt?.capture }),
      removeEventListener: (type, fn) => {
        const i = listeners.findIndex((l) => l.type === type && l.fn === fn);
        if (i >= 0) listeners.splice(i, 1);
      },
      listeners: (type) => listeners.filter((l) => l.type === type),
      dispatch: (type, ev = {}) => {
        const e = { target: el, preventDefault() {}, stopPropagation() {}, ...ev };
        listeners.filter((l) => l.type === type).forEach((l) => l.fn(e));
        return e;
      },
      click: () => { el.clicks++; el.dispatch('click'); },
      focus: () => { doc.activeElement = el; },
      blur: () => { if (doc.activeElement === el) doc.activeElement = doc.body; },
      select() {}, scrollIntoView() {},
      getClientRects: () => (rendered(el) ? [{}] : []),
      getBoundingClientRect: () => ({ left: 0, top: 0, right: 100, bottom: 20, width: 100, height: 20 }),
      getAnimations: () => [],
      offsetWidth: 100, offsetHeight: 20,
      [Symbol.for('nodejs.util.inspect.custom')]: () => `<${tag}${el.id ? `#${el.id}` : ''}>`,
    };
    return el;
  };
  const html = make('html');
  const doc = make('#document');
  Object.assign(doc, {
    body: html.appendChild(make('body')), documentElement: html,
    createElement: (tag) => make(tag),
    createTextNode: (text) => Object.assign(make('#text'), { nodeType: 3, text: String(text) }),
    getElementById: (id) => { const el = byId.get(id); return el && html.contains(el) ? el : null; },
    querySelectorAll: (sel) => select(html, sel),
    querySelector: (sel) => select(html, sel)[0] || null,
    // A keydown from the focused node: the document's capture listeners, then — unless one of
    // them stopped it — its bubbling ones. `prevented`/`stopped` record what they did.
    key: (key, extra = {}) => {
      const ev = { key, code: key, target: doc.activeElement, prevented: false, stopped: false, shiftKey: false,
        preventDefault() { this.prevented = true; }, stopPropagation() { this.stopped = true; }, ...extra };
      const all = doc.listeners('keydown');
      all.filter((l) => l.capture).forEach((l) => l.fn(ev));
      if (!ev.stopped) all.filter((l) => !l.capture).forEach((l) => l.fn(ev));
      return ev;
    },
  });
  doc.activeElement = doc.body;
  const saved = globalThis.document;
  globalThis.document = doc;
  doc.restore = () => { globalThis.document = saved; };
  return doc;
};
