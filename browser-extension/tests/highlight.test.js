// The two injected outliners: lib/highlight.js (the "highlight on page" toggle, which marks
// EVERY grabbable element) and lib/hoverHighlight.js (the single element a hovered list row
// points at). Both run inside the scanned page via chrome.scripting, so they import nothing
// and are driven here over a fabricated document.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { toggleStencilHighlight } from '../src/lib/highlight/highlight.js';
import { highlightSourceOnTab } from '../src/lib/highlight/hoverHighlight.js';
import { installDom } from './helpers/domStub.js';
import { MSG } from '../src/lib/messages.js';

const PAGE = 'https://shop.example/gallery';

// ── A page-lite: attributes, computed background, and a mouseover listener ──
const el = (tag, { bg = 'none', src = '', href = '', rect = { width: 10, height: 10 } } = {}) => {
  const attrs = {};
  const node = {
    tagName: tag.toUpperCase(), nodeType: 1, parentElement: null, attrs, bg, currentSrc: src, src,
    id: '', textContent: '',
    matches: (sel) => sel.split(',').some((s) => s.trim().toUpperCase() === node.tagName),
    getAttribute: (k) => (k === 'href' ? (href || null) : (k in attrs ? attrs[k] : null)),
    setAttribute: (k, v) => { attrs[k] = v; },
    removeAttribute: (k) => { delete attrs[k]; },
    hasAttribute: (k) => k in attrs,
    querySelectorAll: () => [],
    getBoundingClientRect: () => ({ top: 0, left: 0, bottom: rect.height, right: rect.width, ...rect }),
    appendChild: (c) => c,
    remove() { node.removed = true; },
  };
  return node;
};

// `nodes` is the whole page; querySelectorAll('*') walks it, and the marked set is read
// back off the data attributes, exactly as the injected code does.
const page = (nodes, { withChrome = true } = {}) => {
  const sent = [];
  const listeners = {};
  const body = el('body');
  const created = [];
  const document = {
    documentElement: el('html'),
    body,
    head: el('head'),
    getElementById: (id) => created.find((n) => n.id === id && !n.removed) || null,
    createElement: (tag) => { const n = el(tag); created.push(n); return n; },
    querySelectorAll: (sel) => {
      if (sel === '*') return nodes;
      const attr = /^\[(.+)\]$/.exec(sel);
      if (attr) return nodes.filter((n) => n.hasAttribute(attr[1]));
      return nodes.filter((n) => n.matches(sel));
    },
    addEventListener: (t, fn) => { (listeners[t] ||= []).push(fn); },
    removeEventListener: (t, fn) => { listeners[t] = (listeners[t] || []).filter((f) => f !== fn); },
  };
  body.querySelectorAll = document.querySelectorAll;
  const restore = installDom({
    document,
    location: { href: PAGE },
    getComputedStyle: (node) => ({ backgroundImage: node.bg || 'none' }),
    MutationObserver: class { observe() {} disconnect() {} },
    innerWidth: 1000,
    innerHeight: 800,
    window: { innerWidth: 1000, innerHeight: 800, __stencilHlCleanup: null },
    ...(withChrome ? { chrome: { runtime: { sendMessage: (m, cb) => { sent.push(m); if (cb) cb(); }, lastError: null } } } : {}),
  });
  return { sent, created, restore, fire: (t, ev) => { for (const fn of listeners[t] || []) fn(ev); } };
};

const marked = (nodes) => nodes.filter((n) => n.hasAttribute('data-stencil-hl'));

// ── toggleStencilHighlight ──

test('every grabbable element is outlined — images, svg images, videos and backgrounds', () => {
  const nodes = [el('img', { src: '/a.png' }), el('image', { href: '/b.svg' }), el('video'),
                 el('div', { bg: 'url("/hero.jpg")' }), el('p')];
  const p = page(nodes);
  try {
    assert.equal(toggleStencilHighlight(true), 4, 'the bare <p> is not grabbable');
    assert.equal(marked(nodes).length, 4);
  } finally { p.restore(); }
});

test('turning it off clears every mark and drops the stylesheet', () => {
  const nodes = [el('img', { src: '/a.png' }), el('div', { bg: 'url("/b.png")' })];
  const p = page(nodes);
  try {
    toggleStencilHighlight(true);
    const style = p.created.find((n) => n.id === 'stencil-hl-style');
    assert.ok(style, 'the outline rules are injected once');
    assert.equal(toggleStencilHighlight(false), 0);
    assert.deepEqual(marked(nodes), []);
    assert.equal(style.removed, true);
  } finally { p.restore(); }
});

test('the outline colour is the caller\'s, and the hover ring is that colour brightened', () => {
  const p = page([el('img', { src: '/a.png' })]);
  try {
    toggleStencilHighlight(true, '#00a0ff');
    const css = p.created.find((n) => n.id === 'stencil-hl-style').textContent;
    assert.match(css, /outline:2px solid #00a0ff !important/);
    // 0x00 → 71, 0xa0 → 187, 0xff → 255: each channel lifted 28% toward white.
    assert.match(css, /outline:3px solid rgb\(71,187,255\) !important/);
    assert.match(css, /box-shadow:0 0 0 3px rgba\(71,187,255,\.45\)/);
  } finally { p.restore(); }
});

test('a malformed colour falls back to the brand violet rather than emitting broken CSS', () => {
  const p = page([el('img', { src: '/a.png' })]);
  try {
    toggleStencilHighlight(true, 'not-a-colour');
    const css = p.created.find((n) => n.id === 'stencil-hl-style').textContent;
    assert.match(css, /rgb\(161,113,242\)/, 'the hover ring is the default violet, lifted');
  } finally { p.restore(); }
});

test('hovering a grabbable element reports its source URL to the open panel', () => {
  const img = el('img', { src: '/photo.png' });
  const p = page([img]);
  try {
    toggleStencilHighlight(true);
    p.fire('mouseover', { target: img });
    const hover = p.sent.filter((m) => m.type === MSG.HL_HOVER);
    assert.equal(hover.at(-1).source, 'https://shop.example/photo.png');
    assert.ok(img.hasAttribute('data-stencil-hl-hover'), 'and it wears the hover ring');
  } finally { p.restore(); }
});

// NEGATIVE: "whitespace" is usually body's own background. Treating it as a target
// scrolled the panel list to that row every time the cursor left an image.
test('a page-sized background is not a hover target', () => {
  const img = el('img', { src: '/photo.png' });
  const backdrop = el('div', { bg: 'url("/page-bg.png")', rect: { width: 1000, height: 800 } });
  const p = page([img, backdrop]);
  try {
    toggleStencilHighlight(true);
    p.fire('mouseover', { target: img });
    p.fire('mouseover', { target: backdrop });
    assert.equal(p.sent.filter((m) => m.type === MSG.HL_HOVER).at(-1).source, '',
      'leaving an image for the page background clears the row, it does not pick one');
    assert.equal(backdrop.hasAttribute('data-stencil-hl-hover'), false);
    assert.equal(img.hasAttribute('data-stencil-hl-hover'), false);
  } finally { p.restore(); }
});

test('re-running tears the previous run down first — no second listener, no stale marks', () => {
  const img = el('img', { src: '/a.png' });
  const p = page([img]);
  try {
    toggleStencilHighlight(true);
    assert.equal(toggleStencilHighlight(true), 1, 'still exactly one marked element');
    assert.equal(p.created.filter((n) => n.tagName === 'STYLE' && !n.removed).length, 1);
  } finally { p.restore(); }
});

test('a page with no extension messaging still highlights — the reverse hover just goes quiet', () => {
  const img = el('img', { src: '/a.png' });
  const p = page([img], { withChrome: false });
  try {
    assert.equal(toggleStencilHighlight(true), 1);
    p.fire('mouseover', { target: img });
    assert.ok(img.hasAttribute('data-stencil-hl-hover'));
  } finally { p.restore(); }
});

// ── highlightSourceOnTab: the injection wrapper ──

test('the outline is injected into EVERY frame, and true means some frame found it', async () => {
  const calls = [];
  const restore = installDom({
    chrome: { scripting: { executeScript: async (opts) => { calls.push(opts); return [{ result: false }, { result: true }]; } } },
  });
  try {
    assert.equal(await highlightSourceOnTab(7, 'https://x/y.png', '#ff0000'), true);
    assert.deepEqual(calls[0].target, { tabId: 7, allFrames: true });
    assert.deepEqual(calls[0].args, ['https://x/y.png', '#ff0000']);
  } finally { restore(); }
});

test('a falsy source clears the outline, and a restricted page is false, never a throw', async () => {
  const calls = [];
  let restore = installDom({
    chrome: { scripting: { executeScript: async (o) => { calls.push(o); return [{ result: false }]; } } },
  });
  try {
    assert.equal(await highlightSourceOnTab(7, ''), false);
    assert.deepEqual(calls[0].args, ['', '#7c3aed'], 'and it defaults to the brand violet');
  } finally { restore(); }
  restore = installDom({ chrome: { scripting: { executeScript: async () => { throw new Error('cannot access'); } } } });
  try {
    assert.equal(await highlightSourceOnTab(7, 'https://x/y.png'), false);
  } finally { restore(); }
});
