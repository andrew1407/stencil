// The page copy the theme lens shows (js/ui/accent/themeCopy.js): the page's sheets as written,
// re-rooted onto the copy's root and cached; a root that takes the other theme as <html> does,
// carries the page-level attributes and inherits nothing else; nothing in it that could act;
// canvases carrying their pixels (the picture's inverted); the switch showing the other glyph;
// scroll offsets restored once connected; animations frozen.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { createStubElement } from '../../helpers/dom.js';
import { node, walk, byId, SheetStandIn, pageDoc } from '../../helpers/lensDomRig.js';

globalThis.CSSStyleSheet = SheetStandIn;
globalThis.getComputedStyle = (el) => el.computed ?? {};
const {
  COPY_CLASS, PICTURE_FILTER, MAX_COPY_PX, NO_COPY_ATTR, rerootCss, pageSheets, readPageCss, cutInheritance, animatedProps,
  copyPixels, buildPageCopy,
} = await import('../../../js/ui/accent/themeCopy.js');
const { COPY_ATTR } = await import('../../../js/ui/base.js');
const { icon } = await import('../../../js/ui/icons.js');

test(':root selectors answer to the copy\'s root; nothing else moves', () => {
  assert.equal(rerootCss(':root{--a:1}:root[data-accent="x"] .b{c:d}'),
    `.${COPY_CLASS}{--a:1}.${COPY_CLASS}[data-accent="x"] .b{c:d}`);
  assert.equal(rerootCss('html.x body .root-y{}'), 'html.x body .root-y{}');
});

const sheet = (rules, extra = {}) => ({ cssRules: rules.map((cssText) => ({ cssText })), media: { mediaText: '' }, ...extra });

test('the page\'s sheets become adoptable copies, in order, built once until they change', () => {
  const unreadable = { get cssRules() { throw new Error('cross-origin'); }, media: { mediaText: '' } };
  const first = sheet([':root { --x: 1 }', '.a { color: red }'], { href: 'http://h/css/a.css' });
  const print = sheet(['.p {}'], { media: { mediaText: 'print' } });
  const off = sheet(['.off {}'], { disabled: true });
  const doc = pageDoc({ sheets: [first, unreadable, print, off] });
  const sheets = pageSheets(doc);
  assert.equal(sheets.length, 2, 'unreadable and disabled sheets are left out');
  assert.equal(sheets[0].text, `.${COPY_CLASS} { --x: 1 }\n.a { color: red }`);
  assert.deepEqual(sheets[0].opts, { media: '', baseURL: 'http://h/css/a.css' }, 'relative urls resolve as before');
  assert.deepEqual(sheets[1].opts, { media: 'print' }, 'a media-scoped sheet stays scoped');
  assert.equal(pageSheets(doc), sheets, 'unchanged: the same copies');
  first.cssRules.push({ cssText: '.b {}' });
  assert.notEqual(pageSheets(doc), sheets, 'a rule added: built again');
});

// What the CSSOM serializes for `border: 1px solid var(--b); border-bottom-width: 2px`: the var()
// shorthand's longhands come back empty.
const LOSSY = '.k { border-top-color: ; border-bottom-width: 2px; }';

test('a sheet copies as written once its text is read; the CSSOM\'s text stands in until then', async () => {
  const linked = sheet([LOSSY], { href: 'http://h/css/k.css' });
  const styled = sheet([LOSSY], { ownerNode: { textContent: ':root { --b: #ddd }' } });
  const gone = sheet([LOSSY], { href: 'http://h/css/gone.css' });
  const asWritten = '.k { border: 1px solid var(--b); border-bottom-width: 2px; }';
  globalThis.fetch = async (url) => (url === linked.href ? { ok: true, text: async () => asWritten } : { ok: false, status: 404 });
  const doc = pageDoc({ sheets: [linked, styled, gone] });
  const before = pageSheets(doc);
  assert.deepEqual(before.map((c) => c.text), [LOSSY, `.${COPY_CLASS} { --b: #ddd }`, LOSSY], 'a <style> is its own text');
  await readPageCss(doc);
  const after = pageSheets(doc);
  assert.notEqual(after, before, 'the read rebuilds the copies');
  assert.deepEqual(after.map((c) => c.text), [asWritten, `.${COPY_CLASS} { --b: #ddd }`, LOSSY],
    'a sheet that could not be read keeps the CSSOM\'s text');
});

test('the copy\'s root inherits nothing from the page: the host resets every custom property it hands down', () => {
  const host = createStubElement('div');
  const handed = ['color', '--text-label', 'font-size', '--accent'];
  globalThis.getComputedStyle = (el) => (el === 'body' ? handed : {});
  try { cutInheritance(host, 'body'); } finally { globalThis.getComputedStyle = (el) => el.computed ?? {}; }
  assert.equal(host.style['--text-label'], 'initial', 'a light root\'s `--text-label: inherit` finds nothing');
  assert.equal(host.style['--accent'], 'initial', 'the accent comes back from the copy\'s own rules and style');
  assert.equal(host.style.color, undefined, 'the rest is the host rule\'s `all: initial`');
  const css = readFileSync(new URL('../../../css/animations/icon/themeLens.css', import.meta.url), 'utf8');
  const rule = /\.theme-lens \{\s*([^;]+);/.exec(css);
  assert.equal(rule?.[1].trim(), 'all: initial', 'the host resets every other property before it places itself');
});

test('a running animation hands over where things stand and whether they show, never a colour', () => {
  const moving = node('div');
  const glow = node('span');
  const animations = [
    { effect: { target: moving, getKeyframes: () => [{ offset: 0, transform: 'none', opacity: '0', backgroundColor: 'red' }] } },
    { effect: { target: glow, pseudoElement: '::before', getKeyframes: () => [{ opacity: '1' }] } },
  ];
  const held = animatedProps(pageDoc({ animations }));
  assert.deepEqual([...held.get(moving)], ['transform', 'opacity']);
  assert.equal(held.has(glow), false, 'a pseudo-element cannot be handed inline values');
});

test('a canvas copies its pixels; a huge one is copied down at its size on screen; the picture inverts', () => {
  const src = node('canvas', { size: [40, 30] });
  const copy = node('canvas');
  copyPixels(src, copy, true);
  assert.deepEqual([copy.width, copy.height], [40, 30]);
  assert.deepEqual(copy.drawn, [[src, 0, 0, 40, 30]]);
  assert.equal(copy.style.filter, PICTURE_FILTER);
  const huge = node('canvas', { size: [8192, 8192] });
  huge.computed = { width: '1200px', height: '1200px' };
  const small = node('canvas');
  copyPixels(huge, small);
  assert.ok(small.width * small.height <= MAX_COPY_PX);
  assert.deepEqual([small.style.width, small.style.height], ['1200px', '1200px'], 'its box does not move');
  assert.equal(small.style.filter, undefined, 'only the picture is inverted');
  const blank = node('canvas', { size: [0, 0] });
  const untouched = node('canvas');
  copyPixels(blank, untouched);
  assert.deepEqual(untouched.drawn, []);
});

const page = () => {
  const picture = node('canvas', { id: 'canvas', size: [8, 6] });
  const dust = node('canvas', { size: [4, 4] });
  const viewport = node('div', { id: 'canvas-viewport', scroll: [120, 40], kids: [picture] });
  const toggle = node('button', { id: 'theme-toggle', attrs: { onclick: 'x()' } });
  const field = node('input', { id: 'name', attrs: { autofocus: '', onfocus: 'y()' } });
  const video = node('video', { attrs: { src: 'blob:v', autoplay: '', preload: 'auto' }, kids: [node('source', { attrs: { src: 'v.mp4' } })] });
  const region = node('stencil-toolbar', { kids: [toggle, field, video, dust, node('script')] });
  const kids = [node('div', { id: 'root', kids: [region, viewport] }), node('script'), node('div', { attrs: { role: 'tooltip' } }),
    node('div', { attrs: { 'data-drag-ghost': '' } }), node('div', { attrs: { [NO_COPY_ATTR]: '' } })];
  const doc = pageDoc({ kids, html: { lang: 'en', 'data-theme': 'dark', 'data-accent': 'pink', 'data-modal-backdrop': '', style: '--accent: #123456' },
    body: { class: 'controls-collapsed' } });
  return { doc, picture, dust };
};

test('the copy\'s root takes the other theme as <html> does, keeps the page\'s accent and marks itself', () => {
  const { doc } = page();
  const { html } = buildPageCopy(doc, { theme: 'light' });
  assert.equal(html.getAttribute('data-theme'), 'light');
  assert.equal(html.getAttribute('data-accent'), 'pink');
  assert.equal(html.getAttribute('style'), '--accent: #123456', 'a custom accent rides along');
  assert.equal(html.getAttribute('lang'), 'en');
  assert.ok(html.hasAttribute('data-modal-backdrop'), 'a root-keyed rule (`:root[data-modal-backdrop] …`) still finds it');
  assert.ok(html.hasAttribute(COPY_ATTR), 'its regions are pictures, never wired');
  assert.ok(html.classes.has(COPY_CLASS));
  const body = html.children[0];
  assert.equal(body.getAttribute('class'), 'controls-collapsed');
  assert.deepEqual(body.children.map((n) => n.id), ['root'], 'no script, tooltip, ghost or lens');
  assert.equal(html.style.top, undefined, 'an unscrolled window leaves the root in place');
  doc.defaultView = { scrollX: 0, scrollY: 300 };
  const scrolled = buildPageCopy(doc, { theme: 'light' }).html;
  assert.deepEqual([scrolled.style.position, scrolled.style.top, scrolled.style.left], ['relative', '-300px', '0px'],
    'a scrolled window: the copy sits where the page does');
});

test('nothing in the copy can act: handlers, autofocus, scripts and media loads are gone', () => {
  const { doc } = page();
  const { html } = buildPageCopy(doc, { theme: 'light' });
  const nodes = walk(html);
  assert.ok(!nodes.some((n) => n.tagName === 'SCRIPT' || n.tagName === 'SOURCE'));
  assert.ok(!nodes.some((n) => n.getAttributeNames().some((a) => /^on/.test(a) || a === 'autofocus')));
  const video = nodes.find((n) => n.tagName === 'VIDEO');
  assert.deepEqual([video.hasAttribute('src'), video.hasAttribute('autoplay'), video.getAttribute('preload')],
    [false, false, 'none']);
});

test('the picture\'s canvases carry their pixels, inverted; the switch shows the other glyph', () => {
  const { doc, picture } = page();
  const { html } = buildPageCopy(doc, { theme: 'dark', isPicture: (c) => c === picture });
  const copied = byId(html, 'canvas');
  assert.deepEqual(copied.drawn, [[picture, 0, 0, 8, 6]]);
  assert.equal(copied.style.filter, PICTURE_FILTER);
  assert.deepEqual(walk(html).filter((n) => n.tagName === 'CANVAS'), [copied], 'every other canvas is motion: left out');
  assert.equal(byId(html, 'theme-toggle').innerHTML, icon('sun'), 'the dark face shows the sun');
  assert.equal(byId(buildPageCopy(doc, { theme: 'light' }).html, 'theme-toggle').innerHTML, icon('moon'));
});

test('no tooltip, tip cloud or dust layer is ever in a copy, at any depth', () => {
  const picture = node('canvas', { id: 'canvas', size: [8, 6] });
  const kids = [
    node('div', { id: 'root', kids: [picture, node('canvas', { attrs: { class: 'canvas-dust' }, size: [9, 9] }),
      node('div', { attrs: { class: 'disintegrate-host' }, kids: [node('canvas', { size: [9, 9] })] }),
      node('div', { attrs: { class: 'chat-status-tip' } })] }),
    node('div', { id: 'app-tooltip', attrs: { role: 'tooltip' } }),
    node('stencil-tooltip', { id: 'tooltip', attrs: { class: 'tooltip' } }),
    node('div', { attrs: { class: 'disintegrate-host' } }),
    node('canvas', { attrs: { class: 'swap-dust' }, size: [20, 20] }),
  ];
  const { html } = buildPageCopy(pageDoc({ kids }), { theme: 'light', isPicture: (c) => c === picture });
  const left = walk(html).slice(2);
  assert.deepEqual(left.map((n) => n.id || n.tagName), ['root', 'canvas']);
});

test('a region or window not shown is copied as an empty box: its siblings keep their places', () => {
  const closed = node('div', { id: 'modal', kids: [node('div', { kids: [node('span')] })] });
  closed.computed = { display: 'none' };
  const open = node('div', { id: 'panel', kids: [node('span', { id: 'kept' })] });
  const { html } = buildPageCopy(pageDoc({ kids: [node('div', { id: 'root', kids: [closed, open] })] }), { theme: 'light' });
  assert.deepEqual(byId(html, 'modal').children, []);
  assert.ok(byId(html, 'kept'));
});

test('scroll offsets wait for the copy to be connected, then match the page', () => {
  const { doc } = page();
  const { html, settle } = buildPageCopy(doc, { theme: 'light' });
  const vp = byId(html, 'canvas-viewport');
  assert.deepEqual([vp.scrollTop, vp.scrollLeft], [0, 0]);
  settle();
  assert.deepEqual([vp.scrollTop, vp.scrollLeft], [120, 40]);
});

test('an element mid-animation is copied as it stands now', () => {
  const { doc } = page();
  const root = doc.body.children[0];
  root.computed = { transform: 'matrix(1, 0, 0, 1, 0, 12)', opacity: '0.5' };
  doc.getAnimations = () => [{ effect: { target: root, getKeyframes: () => [{ transform: 'none', opacity: '1' }] } }];
  const copy = byId(buildPageCopy(doc, { theme: 'light' }).html, 'root');
  assert.deepEqual([copy.style.transform, copy.style.opacity], ['matrix(1, 0, 0, 1, 0, 12)', '0.5']);
});
