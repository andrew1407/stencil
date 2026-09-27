// The fabricated page the ctx*.test.js suites run the right-click probe over. Both files are
// classic IIFE content scripts (no exports, no imports), loaded in manifest order into one vm:
// loading them IS arming the probe, and the listeners it binds are the surface under test.
import vm from 'node:vm';
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { MSG } from '../../src/lib/messages.js';

const read = (rel) => readFileSync(fileURLToPath(new URL(`../../${rel}`, import.meta.url)), 'utf8');
const manifest = JSON.parse(read('manifest.json'));
export const PROBE_FILES = manifest.content_scripts[0].js;
export const SOURCES = PROBE_FILES.map(read);
export const PAGE = 'https://shop.example/gallery';

// ── A page-lite: exactly the surface the probe reaches for ──
export const el = (tag, props = {}) => {
  const node = {
    tagName: tag.toUpperCase(), nodeType: 1, parentElement: null, attrs: {}, children: [],
    rect: { x: 0, y: 0, left: 0, top: 0, right: 0, bottom: 0, width: 0, height: 0 },
    bg: 'none',
    getAttribute: (k) => (k in node.attrs ? node.attrs[k] : null),
    getBoundingClientRect: () => node.rect,
    // Tag selectors only, with any attribute filter ('a[href]') taken as read.
    matches: (sel) => sel.split(',').some((s) => s.trim().split('[')[0].toUpperCase() === node.tagName),
    closest(sel) {
      for (let n = node; n; n = n.parentElement) if (n.matches(sel)) return n;
      return null;
    },
    ownerDocument: null,
    ...props,
  };
  return node;
};
export const at = (node, x, y, w, h) => {
  node.rect = { x, y, left: x, top: y, right: x + w, bottom: y + h, width: w, height: h };
  return node;
};
export const chain = (...nodes) => {
  for (let i = 1; i < nodes.length; i++) nodes[i - 1].parentElement = nodes[i];
  return nodes[0];
};

// Load the probe over a page holding `videos`, with `stack` under the cursor point.
export const probe = ({ videos = [], stack = [], canvas = null } = {}) => {
  const sent = [];
  const listeners = {};
  const options = {};
  const timers = new Map();
  const cost = { styles: 0, observers: 0 };
  let nextTimer = 1;
  const document = {
    documentElement: el('html'),
    addEventListener: (t, fn, opts) => { (listeners[t] ||= []).push(fn); (options[t] ||= []).push(opts); },
    querySelectorAll: (sel) => (sel === 'video' ? videos : []),
    elementsFromPoint: () => stack,
    createElement: () => canvas || { getContext: () => ({ drawImage() {} }), toDataURL: () => 'data:image/jpeg;base64,FRAME' },
  };
  const sandbox = {
    window: { __proto__: null, devicePixelRatio: 2, parent: null },
    document,
    location: { href: PAGE },
    URL,
    Date,
    Math,
    getComputedStyle: (node) => { cost.styles++; return { backgroundImage: node.bg || 'none' }; },
    MutationObserver: class { constructor() { cost.observers++; } observe() {} },
    setTimeout: (fn, ms) => { timers.set(nextTimer, { fn, ms }); return nextTimer++; },
    clearTimeout: (id) => { timers.delete(id); },
    chrome: { runtime: { sendMessage: (m, cb) => { sent.push(m); if (cb) cb(); }, lastError: null } },
  };
  sandbox.window.document = document;
  // topRect walks out through the frame chain, so a video has to know its window.
  for (const v of videos) v.ownerDocument = { defaultView: sandbox.window };
  vm.createContext(sandbox);
  for (const src of SOURCES) vm.runInContext(src, sandbox);
  const fire = (type, ev) => { for (const fn of listeners[type] || []) fn(ev); };
  // The dwell: whatever is still armed fires, as if the pointer had come to rest.
  const rest = () => { for (const [id, { fn }] of [...timers]) { timers.delete(id); fn(); } };
  // The probe reports through sendMessage; the last CTX payload is its verdict. The
  // payload crosses the vm realm, so it is compared by value, not by prototype.
  const resolve = (target, x = 10, y = 10) => {
    fire('contextmenu', { target, clientX: x, clientY: y });
    const msg = sent.filter((m) => m.type === MSG.CTX).at(-1);
    return JSON.parse(JSON.stringify({ data: msg.data ?? null }));
  };
  const ctxSent = () => sent.filter((m) => m.type === MSG.CTX);
  return { sent, fire, rest, resolve, listeners, options, timers, cost, ctxSent, sandbox };
};
