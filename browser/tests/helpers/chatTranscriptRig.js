// Shared rig for the chatTranscriptRender.test.js family: a live-enough element tree, since
// the structural bugs here (a doubled bubble, a row missing its trigger) need a real DOM.

export const matchesSel = (el, sel) => {
  if (sel.startsWith('.')) return el.classList.contains(sel.slice(1));
  const m = /^\[([\w-]+)(?:="([^"]*)")?\]$/.exec(sel);
  if (!m) return false;
  const key = m[1].replace(/^data-/, '').replace(/-(\w)/g, (_, c) => c.toUpperCase());
  const v = el.dataset[key];
  return m[2] === undefined ? v !== undefined : String(v) === m[2];
};
export const descendants = (el, out = []) => {
  for (const c of el.children) { out.push(c); descendants(c, out); }
  return out;
};

export const makeEl = (tag = 'div') => {
  const classes = new Set();
  const el = {
    tagName: String(tag).toUpperCase(), children: [], parentNode: null,
    // dataset stringifies, like the real one — renderChatLog compares its keys as strings.
    dataset: new Proxy({}, { set: (t, k, v) => { t[k] = String(v); return true; } }),
    _text: '', innerHTML: '', title: '', type: '', tabIndex: 0,
    scrollTop: 0, scrollHeight: 0, clientHeight: 0,
    style: { setProperty() {}, getPropertyValue() { return ''; } },
    get className() { return [...classes].join(' '); },
    set className(v) { classes.clear(); for (const c of String(v).split(/\s+/)) if (c) classes.add(c); },
    classList: {
      add: (...cs) => cs.forEach((c) => classes.add(c)),
      remove: (...cs) => cs.forEach((c) => classes.delete(c)),
      toggle: (c, on) => (on ? classes.add(c) : classes.delete(c)),
      contains: (c) => classes.has(c),
    },
    set textContent(v) { el._text = String(v); el.children.length = 0; },
    get textContent() { return el._text + el.children.map((c) => c.textContent).join(''); },
    setAttribute() {}, removeAttribute(name) { delete el.dataset[name.replace(/^data-/, '')]; },
    addEventListener(t, fn) { (el._l ||= {})[t] = [...(el._l?.[t] || []), fn]; },
    removeEventListener() {},
    fire(t, ev = {}) { for (const fn of el._l?.[t] || []) fn(ev); },
    appendChild(c) { if (c.parentNode) c.remove(); c.parentNode = el; el.children.push(c); return c; },
    append(...cs) { for (const c of cs) el.appendChild(c); },
    prepend(c) { c.parentNode = el; el.children.unshift(c); return c; },
    before(node) { const k = el.parentNode.children; k.splice(k.indexOf(el), 0, node); node.parentNode = el.parentNode; },
    after(node) { const k = el.parentNode.children; k.splice(k.indexOf(el) + 1, 0, node); node.parentNode = el.parentNode; },
    remove() {
      const k = el.parentNode?.children;
      const i = k ? k.indexOf(el) : -1;
      if (i >= 0) k.splice(i, 1);
      el.parentNode = null;
    },
    contains(n) { return n === el || descendants(el).includes(n); },
    closest(sel) {
      for (let n = el; n; n = n.parentNode) if (matchesSel(n, sel)) return n;
      return null;
    },
    querySelector(sel) { return descendants(el).find((d) => matchesSel(d, sel)) || null; },
    querySelectorAll(sel) { return descendants(el).filter((d) => matchesSel(d, sel)); },
  };
  return el;
};

export const stubDom = () => {
  globalThis.document = { createElement: makeEl, body: makeEl('body'), getElementById: () => null,
    addEventListener() {}, removeEventListener() {} };
  globalThis.window = { innerWidth: 1024, innerHeight: 768, addEventListener() {}, removeEventListener() {} };
};

export const rowsOf = (transcript) => transcript.children.filter((c) => c.classList.contains('chat-msg'));
export const textNodesOf = (rowEl) => descendants(rowEl).filter((d) => d.classList.contains('chat-msg-text'));

// The prompt of the reported doubled-bubble turn.
export const PROMPT = 'upload in incognito mode, bame b&w, crop to portrait, turn on horz comparison';
