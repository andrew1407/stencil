// Both chat surfaces wired for real against a permissive DOM: the docked panel
// (StencilChatPanel.wire) and the context-menu flyout (wireCtxAssistantChat). Every id
// resolves to a live element, so the suites drive clicks, right-clicks and turns end to end.
import { createMemoryStorage } from './memoryStorage.js';

const parts = (sel) => /^([a-z-]*)((?:[.#][\w-]+)*)((?:\[[\w-]+(?:="[^"]*")?\])*)$/i.exec(sel.trim());
const matches = (el, sel) => {
  const m = parts(sel);
  if (!m || !el?.classList) return false;
  if (m[1] && el.tagName !== m[1].toUpperCase()) return false;
  for (const tok of m[2].match(/[.#][\w-]+/g) || []) {
    if (tok[0] === '.' ? !el.classList.contains(tok.slice(1)) : el.id !== tok.slice(1)) return false;
  }
  for (const [, k, v] of m[3].matchAll(/\[([\w-]+)(?:="([^"]*)")?\]/g)) {
    const key = k.replace(/^data-/, '').replace(/-(\w)/g, (_, c) => c.toUpperCase());
    const got = k.startsWith('data-') ? el.dataset[key] : el.getAttribute(k);
    if (got === undefined || got === null || (v !== undefined && String(got) !== v)) return false;
  }
  return true;
};
const descendants = (el, out = []) => {
  for (const c of el.children) { out.push(c); descendants(c, out); }
  return out;
};
const select = (root, sel) => {
  const scoped = /^:scope > (.+)$/.exec(sel);
  if (scoped) return root.children.filter((c) => matches(c, scoped[1]));
  const alts = sel.split(',').map((s) => s.trim().split(/\s+/).pop());
  return descendants(root).filter((d) => alts.some((s) => matches(d, s)));
};

export const makeEl = (tag = 'div') => {
  const classes = new Set();
  const attrs = new Map();
  const listeners = {};
  const style = { setProperty(k, v) { style[k] = v; }, removeProperty(k) { delete style[k]; },
    getPropertyValue(k) { return style[k] ?? ''; } };
  const el = {
    tagName: String(tag).toUpperCase(), nodeType: 1, id: '', children: [], parentNode: null,
    dataset: new Proxy({}, { set: (t, k, v) => { t[k] = String(v); return true; } }),
    style, listeners, value: '', title: '', type: '', disabled: false, hidden: false, checked: false,
    innerHTML: '', _text: '', scrollTop: 0, scrollHeight: 0, clientHeight: 0, offsetWidth: 120, offsetHeight: 100,
    rect: { left: 0, top: 0, right: 0, bottom: 0, width: 0, height: 0 },
    get className() { return [...classes].join(' '); },
    set className(v) { classes.clear(); for (const c of String(v).split(/\s+/)) if (c) classes.add(c); },
    classList: {
      add: (...cs) => cs.forEach((c) => classes.add(c)),
      remove: (...cs) => cs.forEach((c) => classes.delete(c)),
      toggle: (c, on) => { const w = on === undefined ? !classes.has(c) : !!on; w ? classes.add(c) : classes.delete(c); return w; },
      contains: (c) => classes.has(c),
    },
    set textContent(v) { el._text = String(v); el.children.length = 0; },
    get textContent() { return el._text + el.children.map((c) => c.textContent).join(''); },
    setAttribute: (k, v) => attrs.set(k, String(v)),
    getAttribute: (k) => (attrs.has(k) ? attrs.get(k) : null),
    hasAttribute: (k) => attrs.has(k),
    removeAttribute: (k) => { attrs.delete(k); delete el.dataset[k.replace(/^data-/, '')]; },
    toggleAttribute: (k, on) => { const w = on === undefined ? !attrs.has(k) : !!on; w ? attrs.set(k, '') : attrs.delete(k); return w; },
    addEventListener(t, fn) { (listeners[t] ||= []).push(fn); },
    removeEventListener(t, fn) { const a = listeners[t] || []; const i = a.indexOf(fn); if (i >= 0) a.splice(i, 1); },
    fire(t, ev = {}) {
      const e = { target: el, preventDefault() { e.prevented = true; }, stopPropagation() { e.stopped = true; }, ...ev };
      for (const fn of [...(listeners[t] || [])]) fn(e);
      return e;
    },
    dispatchEvent(e) { el.fire(e.type, e); return true; },
    appendChild(c) { if (c.parentNode) c.remove(); c.parentNode = el; el.children.push(c); return c; },
    append(...cs) { for (const c of cs) el.appendChild(c); },
    prepend(c) { if (c.parentNode) c.remove(); c.parentNode = el; el.children.unshift(c); return c; },
    before(n) { const k = el.parentNode.children; if (n.parentNode) n.remove(); k.splice(k.indexOf(el), 0, n); n.parentNode = el.parentNode; },
    after(n) { const k = el.parentNode.children; if (n.parentNode) n.remove(); k.splice(k.indexOf(el) + 1, 0, n); n.parentNode = el.parentNode; },
    insertBefore(n, at) { if (!at) return el.appendChild(n); at.before(n); return n; },
    removeChild(c) { c.remove(); return c; },
    remove() {
      const k = el.parentNode?.children;
      const i = k ? k.indexOf(el) : -1;
      if (i >= 0) k.splice(i, 1);
      el.parentNode = null;
    },
    get parentElement() { return el.parentNode; },
    get childNodes() { return el.children; },
    get firstElementChild() { return el.children[0] ?? null; },
    get lastElementChild() { return el.children[el.children.length - 1] ?? null; },
    get childElementCount() { return el.children.length; },
    contains(n) { return n === el || descendants(el).includes(n); },
    closest(sel) { for (let n = el; n; n = n.parentNode) if (matches(n, sel)) return n; return null; },
    matches: (sel) => sel.split(',').some((s) => matches(el, s)),
    querySelector: (sel) => select(el, sel)[0] || null,
    querySelectorAll: (sel) => select(el, sel),
    insertAdjacentHTML() {}, insertAdjacentElement() {},
    getBoundingClientRect: () => ({ ...el.rect }),
    focus() { if (globalThis.document) globalThis.document.activeElement = el; },
    blur() {}, click() { el.fire('click'); }, select() {}, scrollTo() {}, scrollIntoView() {},
    setPointerCapture() {}, releasePointerCapture() {}, hasPointerCapture: () => false,
    animate: () => ({ cancel() {}, finished: Promise.resolve(), onfinish: null }),
    getContext: () => null,
  };
  return el;
};

// A document whose getElementById mints a live element on first ask, so every wirer finds
// the node it reaches for; the flyout and panel roots are seeded to hold their own children.
export const installChatDom = () => {
  const els = new Map();
  const doc = makeEl('#document');
  doc.body = makeEl('body');
  doc.documentElement = makeEl('html');
  doc.activeElement = null;
  doc.createElement = makeEl;
  doc.createTextNode = (t) => ({ nodeType: 3, textContent: String(t), parentNode: null, remove() {} });
  doc.getElementById = (id) => {
    if (!els.has(id)) { const e = makeEl(); e.id = id; els.set(id, e); }
    return els.get(id);
  };
  doc.querySelectorAll = (sel) => select(doc.body, sel);
  doc.querySelector = (sel) => select(doc.body, sel)[0] || null;
  const $ = doc.getElementById;
  const flyout = $('ctx-assist-sub');
  flyout.className = 'ctx-sub ctx-assist-sub';
  flyout.append($('ctx-assist-transcript'), $('ctx-assist-sizer'), $('ctx-assist-input'), $('ctx-assist-send'));
  $('ctx-assist-menu').append(flyout);
  $('ctx-menu').append($('ctx-assist-menu'));
  $('chat-panel').append($('chat-transcript'), $('chat-input-sizer'), $('chat-input'), $('chat-send'));
  for (const prefix of ['chat', 'ctx-assist']) {
    const menu = $(`${prefix}-more-menu`);
    const item = makeEl('button');
    item.className = 'chat-more-item';
    menu.hidden = true;
    menu.append(item);
  }
  globalThis.document = doc;
  globalThis.window = Object.assign(makeEl('#window'), { innerWidth: 1024, innerHeight: 768,
    getComputedStyle: () => ({ getPropertyValue: () => '' }) });
  globalThis.MutationObserver = class { observe() {} disconnect() {} };
  globalThis.ResizeObserver = class { observe() {} unobserve() {} disconnect() {} };
  globalThis.localStorage = createMemoryStorage();
  return doc;
};

// Notices land here: the rig registers #notify-balloon as a recorder.
export const recordNotices = (doc) => {
  const seen = [];
  doc.getElementById('notify-balloon').notify = (text, type, opts) => seen.push({ text, type, opts });
  return seen;
};

// A controller whose send holds until the test settles it, recording each prompt and the
// queue it carried; `settle(entry)` answers, `fail(err)` rejects.
export const scriptedController = () => {
  const ctrl = { attachments: [], sent: [], carried: [], signals: [], settle: null, fail: null, requeued: 0, cleared: 0,
    send: (text, { signal } = {}) => {
      ctrl.sent.push(text);
      ctrl.signals.push(signal);
      ctrl.carried.push(ctrl.attachments.map((a) => a.name));
      return new Promise((res, rej) => { ctrl.settle = res; ctrl.fail = rej; });
    },
    addAttachment: async (file) => { ctrl.attachments.push({ name: file.name, kind: 'image', dataUrl: 'data:image/png;base64,AA' }); },
    removeAttachment: (i) => { ctrl.attachments.splice(i, 1); },
    requeueLastTurnAttachments() { ctrl.requeued += 1; },
    clearConversation() { ctrl.cleared += 1; } };
  return ctrl;
};

// Both surfaces over one fresh log and one scripted controller, as the app wires them.
export const wireBothSurfaces = async ({ backlog = [] } = {}) => {
  const doc = installChatDom();
  const notices = recordNotices(doc);
  const session = await import('../../js/llm/chat/session.js');
  session.resetChatLog();
  for (const row of backlog) session.appendChatRow(row);
  const app = { voice: null };
  const ctrl = scriptedController();
  session.sharedChatController(app, { create: () => ctrl });
  const { StencilChatPanel } = await import('../../js/ui/chat/panel.js');
  const panelHost = doc.getElementById('chat-panel');
  StencilChatPanel.prototype.wire.call(panelHost, app);
  const { wireCtxAssistantChat } = await import('../../js/ui/ctx/assistantChat.js');
  const host = flyoutHost();
  const handovers = [];
  wireCtxAssistantChat(app, host, () => handovers.push('panel'));
  const $ = (id) => doc.getElementById(id);
  return {
    doc, app, ctrl, notices, session, host, handovers,
    panel: { host: panelHost, transcript: $('chat-transcript'), input: $('chat-input'), send: $('chat-send') },
    flyout: { el: $('ctx-assist-sub'), item: $('ctx-assist-menu'), transcript: $('ctx-assist-transcript'),
      input: $('ctx-assist-input'), send: $('ctx-assist-send') },
  };
};

// A prompt typed into a composer and sent with Enter, the keyboard's own path.
export const typeAndSend = (surface, text) => {
  surface.input.value = text;
  surface.input.fire('keydown', { key: 'Enter' });
};

// The transcript's element for a log row, and a right-click on it.
export const rowEl = (transcript, id) => transcript.querySelector(`[data-row="${id}"]`);
export const rightClick = (transcript, el, x = 40, y = 60) =>
  transcript.fire('contextmenu', { target: el, clientX: x, clientY: y });
export const openRowMenu = () => document.body.children.find((c) => c.classList.contains('chat-row-menu')) || null;
export const clickRowItem = (menu, label) => menu.children
  .find((b) => b.children[0]?.textContent === label)
  .fire('click');

// The flyout's host: the menu state assistantChat.js drives, recorded.
export const flyoutHost = () => {
  const h = { open: true, busy: 0, closed: 0, sendingFlag: false, positioned: 0,
    menu: document.getElementById('ctx-menu'),
    menuIsOpen: () => h.open, closeMenu: () => { h.closed += 1; h.open = false; },
    sending: () => h.sendingFlag, setSending: (v) => { h.sendingFlag = v; },
    bumpBusy: () => { h.busy += 1; }, positionSub: () => { h.positioned += 1; }, setActiveSub() {},
    setOnMenuClose: (fn) => { h.onMenuClose = fn; } };
  return h;
};
