// Shared DOM-lite stubs for the browser test suites.
//
// Node has no DOM, so two dozen suites each grew their own element factory and
// `globalThis.document` — with slightly different surfaces (some listeners were maps,
// some objects; some classLists were variadic, some not). This module is the superset
// of what those suites actually exercise: NOT a jsdom clone, just the properties the
// components under test touch, implemented once. A suite with a genuinely bespoke
// behaviour keeps it as an `overrides` entry, not as a fork of the whole factory.
//
// Three ways in:
//   createStubElement()  — one element to hand to a component directly.
//   createStubDocument() — a document stub (itself an element, so it can listen).
//   installDom()         — the document assigned to globalThis.document, plus any
//                          window/location/history the suite needs. Call .restore()
//                          to put the previous globals back — though Node runs each
//                          test FILE in its own process, so a module-level install
//                          cannot leak across suites and usually needs no teardown.

/**
 * A stub element: classList backed by the exposed `classes` Set, attributes in the
 * exposed `attrs` Map, listeners in the exposed by-type `listeners` object (deduped,
 * fired via `dispatch(type, ev)` with the element as the default `target`), children
 * with parent tracking (append/remove/replace/insertBefore over one flat `children`), a
 * style object that also speaks set/get/removeProperty, and
 * recorded `focusCalls`. Everything else (innerHTML, textContent, dataset, value…)
 * is a plain writable field.
 * @param {string} tag - Element tag, uppercased into `tagName`.
 * @param {object} overrides - Spread onto the element last: the place for a suite's
 *   quirks (a fixed getBoundingClientRect, `contains: () => true`, …).
 */
export const createStubElement = (tag = 'div', overrides = {}) => {
  const classes = new Set();
  const attrs = new Map();
  const listeners = {};
  const style = {
    setProperty(k, v) { style[k] = v; },
    removeProperty(k) { delete style[k]; },
    getPropertyValue(k) { return style[k] ?? ''; },
  };
  const el = {
    tagName: String(tag).toUpperCase(),
    id: '', hidden: false, disabled: false, value: '', title: '',
    innerHTML: '', textContent: '',
    style, dataset: {}, classes, attrs, listeners,
    children: [], parentNode: null, parentElement: null,
    focused: false, focusCalls: [],

    get className() { return [...classes].join(' '); },
    set className(v) { classes.clear(); for (const c of String(v).split(/\s+/)) if (c) classes.add(c); },
    classList: {
      add: (...cs) => cs.forEach((c) => classes.add(c)),
      remove: (...cs) => cs.forEach((c) => classes.delete(c)),
      contains: (c) => classes.has(c),
      toggle: (c, on) => {
        const want = on === undefined ? !classes.has(c) : !!on;
        want ? classes.add(c) : classes.delete(c);
        return want;
      },
    },

    setAttribute: (k, v) => attrs.set(k, v),
    getAttribute: (k) => (attrs.has(k) ? attrs.get(k) : null),
    hasAttribute: (k) => attrs.has(k),
    removeAttribute: (k) => attrs.delete(k),
    // A bare presence flag, like data-accent-light.
    toggleAttribute: (k, on) => {
      const want = on === undefined ? !attrs.has(k) : !!on;
      want ? attrs.set(k, '') : attrs.delete(k);
      return want;
    },

    get childElementCount() { return el.children.length; },
    get firstElementChild() { return el.children[0] ?? null; },
    appendChild(c) {
      if (c && typeof c === 'object') { c.parentNode = el; c.parentElement = el; }
      el.children.push(c);
      return c;
    },
    append(...cs) { cs.forEach((c) => el.appendChild(c)); },
    // childNodes IS `children` here: appendChild takes a text node as readily as an element,
    // and the components under test walk one flat stream of both.
    get childNodes() { return el.children; },
    get firstChild() { return el.children[0] ?? null; },
    removeChild(c) {
      const i = el.children.indexOf(c);
      if (i >= 0) el.children.splice(i, 1);
      return c;
    },
    replaceChild(next, old) {
      const i = el.children.indexOf(old);
      if (i >= 0) el.children.splice(i, 1, next);
      return old;
    },
    insertBefore(next, at) {
      const i = at === null || at === undefined ? -1 : el.children.indexOf(at);
      el.children.splice(i < 0 ? el.children.length : i, 0, next);
      return next;
    },
    remove() {
      const kids = el.parentNode?.children;
      const i = kids ? kids.indexOf(el) : -1;
      if (i >= 0) kids.splice(i, 1);
      el.parentNode = null;
      el.parentElement = null;
    },
    contains(node) { return node === el || el.children.some((c) => c.contains?.(node)); },

    // Real-DOM semantics: the same (type, fn) pair registers once.
    addEventListener(t, fn) {
      const a = (listeners[t] ||= []);
      if (!a.includes(fn)) a.push(fn);
    },
    removeEventListener(t, fn) {
      const a = listeners[t] || [];
      const i = a.indexOf(fn);
      if (i >= 0) a.splice(i, 1);
    },
    // An event always has a target; a BUBBLED one names the descendant it came from
    // (pass `target` explicitly for those).
    dispatch(t, ev = {}) { for (const fn of [...(listeners[t] || [])]) fn({ target: el, ...ev }); },

    focus(opts) { el.focused = true; el.focusCalls.push(opts); },
    blur() { el.focused = false; },
    getBoundingClientRect: () => ({ left: 0, top: 0, right: 0, bottom: 0, width: 0, height: 0 }),
    getContext: () => null,
    querySelector: () => null,
    querySelectorAll: () => [],
    closest: () => null,
    matches: () => false,
    insertAdjacentElement: () => {},
    ...overrides,
  };
  return el;
};

/**
 * A stub document — built ON a stub element, so it also carries listeners/dispatch.
 * getElementById reads the exposed `els` Map; `register(id, node)` seeds it, or pass
 * `autoCreateById: true` to mint a fresh element per unseen id (the updateButtons-style
 * rigs, where the code under test sets .disabled/.style on whatever it reaches for).
 * @param {{autoCreateById?: boolean} & object} opts - `autoCreateById` plus overrides
 *   spread onto the document (a custom createElement, querySelectorAll, …).
 */
export const createStubDocument = ({ autoCreateById = false, ...overrides } = {}) => {
  const els = new Map();
  return createStubElement('#document', {
    els,
    body: createStubElement('body'),
    documentElement: createStubElement('html'),
    head: createStubElement('head'),
    activeElement: null,
    createElement: (tag) => createStubElement(tag),
    getElementById: (id) => {
      if (!els.has(id) && autoCreateById) els.set(id, createStubElement('div', { id }));
      return els.get(id) ?? null;
    },
    register: (id, node) => { node.id = id; els.set(id, node); return node; },
    ...overrides,
  });
};

/**
 * Install a stub document as globalThis.document, plus any other globals the suite
 * needs alongside it (window / location / history — passed through verbatim).
 * @param {object} docOpts - Forwarded to createStubDocument.
 * @param {object} globals - Extra globals to install, e.g. `{ window: {...} }`.
 * @returns the document, with an extra `restore()` that undoes every install.
 */
export const installDom = (docOpts = {}, globals = {}) => {
  const doc = createStubDocument(docOpts);
  const saved = new Map();
  const set = (key, value) => {
    saved.set(key, Object.getOwnPropertyDescriptor(globalThis, key) ?? null);
    globalThis[key] = value;
  };
  set('document', doc);
  for (const [key, value] of Object.entries(globals)) {
    if (value !== undefined) set(key, value);
  }
  doc.restore = () => {
    for (const [key, desc] of saved) {
      if (desc) Object.defineProperty(globalThis, key, desc);
      else delete globalThis[key];
    }
  };
  return doc;
};
