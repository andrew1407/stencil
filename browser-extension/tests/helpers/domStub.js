// One stub DOM for every suite that drives a module against a document instead of a page.
// Node has no DOM and this repo adds no deps, so this is the union of the element and document
// mocks those suites need — nothing speculative beyond it.
// Defaults are deliberately inert (zero rect, no children, querySelector → null); a suite that
// needs a live measurement sets `el.rect` / `el.offsetWidth` itself.

/**
 * One element. `tag` sets both `tag` and `tagName`; everything else is an override for a
 * default (`rect`, `offsetWidth`, `offsetHeight`, `getContext`, …).
 */
export const stubEl = (tag = 'div', opts = {}) => {
  const classes = new Set();
  const attrs = {};
  const handlers = {};
  let html = '';
  const el = {
    tag,
    tagName: String(tag).toUpperCase(),
    type: '', textContent: '', title: '', value: '', id: '', src: '',
    hidden: false, tabIndex: 0, removed: false, isConnected: true,
    dataset: {}, children: [], parent: null, classes, handlers, attrs,
    offsetWidth: 0, offsetHeight: 0,
    rect: { left: 0, top: 0, right: 0, bottom: 0, width: 0, height: 0 },
    style: {
      cssText: '',
      props: {},
      setProperty(k, v) { this.props[k] = v; },
      removeProperty(k) { delete this.props[k]; },
      getPropertyValue(k) { return this.props[k] ?? ''; },
    },
    classList: {
      add: (...c) => c.forEach((x) => classes.add(x)),
      remove: (...c) => c.forEach((x) => classes.delete(x)),
      contains: (c) => classes.has(c),
      toggle: (c, on) => { (on ?? !classes.has(c)) ? classes.add(c) : classes.delete(c); return classes.has(c); },
    },
    get className() { return [...classes].join(' '); },
    set className(v) { classes.clear(); for (const c of String(v).split(/\s+/)) if (c) classes.add(c); },
    get innerHTML() { return html; },
    set innerHTML(v) { html = v; if (v === '') el.children = []; },
    get childElementCount() { return el.children.length; },
    append: (...n) => { for (const c of n) { if (c) c.parent = el; el.children.push(c); } },
    appendChild: (c) => { if (c) c.parent = el; el.children.push(c); return c; },
    prepend: (...n) => { for (const c of [...n].reverse()) { if (c) c.parent = el; el.children.unshift(c); } },
    insertAdjacentElement() {},
    remove: () => { el.removed = true; el.isConnected = false; el.parent = null; },
    contains: (n) => n === el || el.children.includes(n),
    setAttribute: (k, v) => { attrs[k] = v; },
    getAttribute: (k) => (k in attrs ? attrs[k] : null),
    removeAttribute: (k) => { delete attrs[k]; },
    addEventListener: (t, fn) => { (handlers[t] ||= []).push(fn); },
    removeEventListener: (t, fn) => { handlers[t] = (handlers[t] || []).filter((f) => f !== fn); },
    getBoundingClientRect: () => el.rect,
    querySelector: () => null,
    querySelectorAll: () => [],
    closest: () => null,
    matches: () => false,
    attachShadow: () => el,
    getContext: () => null,
    // Run the handlers a real event would, with the event the caller wants to model.
    fire: (t, ev = {}) => { for (const fn of [...(handlers[t] || [])]) fn(ev); },
    dispatch: (t, ev = {}) => { for (const fn of [...(handlers[t] || [])]) fn({ target: el, ...ev }); },
    click: () => el.fire('click', { stopPropagation: () => { el.stopped = true; } }),
  };
  return Object.assign(el, opts);
};

/**
 * One document over `stubEl`. `listeners` is a flat array of {type, fn, capture} in call
 * order; `on(type)` narrows it and `fire(type, ev)` runs that type's handlers.
 */
export const stubDoc = (opts = {}) => {
  const listeners = [];
  const make = opts.createElement || ((t) => stubEl(t));
  const doc = {
    listeners,
    body: stubEl('body'),
    head: stubEl('head'),
    documentElement: stubEl('html'),
    createElement: make,
    createElementNS: (_ns, t) => make(t),
    createTextNode: (text) => ({ text, textContent: text, isText: true }),
    getElementById: () => null,
    querySelector: () => null,
    querySelectorAll: () => [],
    // DOM semantics: re-adding the same fn at the same capture is a no-op.
    addEventListener: (type, fn, capture) => {
      if (!listeners.some((l) => l.type === type && l.fn === fn && l.capture === capture))
        listeners.push({ type, fn, capture });
    },
    removeEventListener: (type, fn, capture) => {
      const i = listeners.findIndex((l) => l.type === type && l.fn === fn && l.capture === capture);
      if (i >= 0) listeners.splice(i, 1);
    },
    on: (type) => listeners.filter((l) => l.type === type),
    fire: (type, ev = {}) => { for (const l of [...listeners]) if (l.type === type) l.fn(ev); },
  };
  return Object.assign(doc, opts);
};

/** A window with the surface these suites touch: size, matchMedia, listeners, open(). */
export const stubWin = (opts = {}) => {
  const listeners = {};
  const win = {
    innerWidth: 400, innerHeight: 600, listeners,
    matchMedia: () => ({ matches: false, addEventListener() {}, removeEventListener() {} }),
    addEventListener: (t, fn) => { (listeners[t] ||= []).push(fn); },
    removeEventListener: (t, fn) => { listeners[t] = (listeners[t] || []).filter((f) => f !== fn); },
    fire: (t, ev = {}) => { for (const fn of [...(listeners[t] || [])]) fn(ev); },
    open() {},
  };
  return Object.assign(win, opts);
};

/**
 * Put the given globals in place and hand back the restore. Only the keys passed are
 * touched, and each is put back exactly as it was — missing keys are deleted again.
 */
export const installDom = (globals) => {
  const prior = Object.keys(globals).map((k) => [k, k in globalThis, globalThis[k]]);
  Object.assign(globalThis, globals);
  return () => {
    for (const [k, had, value] of prior) {
      if (had) globalThis[k] = value;
      else delete globalThis[k];
    }
  };
};
