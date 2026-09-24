// The skin's icons (js/ui/webcore/icons.js): a map becomes rect runs, installing it makes
// icon() serve the pixel art, and the live swap redraws what is on the page and back.
import { test, afterEach } from 'node:test';
import assert from 'node:assert/strict';
import PIXELS from '../../../js/config/iconsWebcore.json' with { type: 'json' };
import { icon, DRAW_MODE_ICON, iconSkin } from '../../../js/ui/icons.js';
import { pixelRects, pixelTable, pixelIconSvg, setPixelIcons, swapIconArt } from '../../../js/ui/webcore/icons.js';

afterEach(() => setPixelIcons(false));

test('a row of the map becomes one rect per lit run', () => {
  const out = pixelRects(['..kk..ww........', 'k...............']);
  assert.equal(out,
    '<rect x="2" y="0" width="2" height="1" fill="#000000"/><rect x="6" y="0" width="2" height="1" fill="#ffffff"/>'
    + '<rect x="0" y="1" width="1" height="1" fill="#000000"/>');
  assert.equal(Object.keys(pixelTable()).length, Object.keys(PIXELS.icons).length);
  assert.match(pixelIconSvg('logo', 32), /^<svg xmlns=.* width="32" height="32" shape-rendering="crispEdges">.*<\/svg>$/);
});

test('installed, icon() serves the pixel art on the 16-grid; removed, the line-art is back', () => {
  assert.match(icon('save'), /viewBox="0 0 24 24".*stroke="currentColor"/);
  setPixelIcons(true);
  assert.ok(iconSkin());
  const s = icon('save', { size: 13, cls: 'x' });
  assert.match(s, /^<svg class="ic ic-save x" viewBox="0 0 16 16" width="13" height="13" shape-rendering="crispEdges"/);
  assert.ok(!s.includes('currentColor') && s.includes('<rect'), 'the colour is baked');
  assert.ok(!s.includes('`') && !s.includes('${'), 'a pure string');
  assert.match(DRAW_MODE_ICON.rect, /class="ic ic-draw-mode-rect draw-mode-icon" viewBox="0 0 16 16"/);
  assert.equal(icon('nosuch'), '');
  setPixelIcons(false);
  assert.equal(iconSkin(), null);
  assert.match(icon('save'), /stroke="currentColor"/);
  assert.match(DRAW_MODE_ICON.rect, /currentColor/);
});

// A DOM-lite svg whose outerHTML setter re-parses class, width and stroke-width from the markup.
const fakeSvg = (markup) => {
  const el = { attrs: {}, classList: new Set() };
  const parse = (html) => {
    el.classList = new Set((/class="([^"]*)"/.exec(html)?.[1] || '').split(' ').filter(Boolean));
    el.classList.contains = (c) => el.classList.has(c);
    const open = /^<svg[^>]*>/.exec(html)?.[0] || '';
    el.attrs = Object.fromEntries([...open.matchAll(/ (viewBox|width|height|stroke-width)="([^"]*)"/g)].map((m) => [m[1], m[2]]));
    el.innerHTML = html.replace(/^<svg[^>]*>/, '').replace(/<\/svg>$/, '');
    el.html = html;
  };
  parse(markup);
  el.getAttribute = (k) => el.attrs[k] ?? null;
  el.setAttribute = (k, v) => { el.attrs[k] = v; };
  el.removeAttribute = (k) => { delete el.attrs[k]; };
  Object.defineProperty(el, 'outerHTML', { get: () => el.html, set: parse });
  return el;
};

test('the live swap redraws every glyph, keeps its size and stroke, and puts it back', () => {
  const glyph = fakeSvg(icon('check', { size: 13, cls: 'accent-check', sw: 3 }));
  const face = fakeSvg(DRAW_MODE_ICON.rect);
  const logo = fakeSvg('<svg class="app-logo" viewBox="0 0 64 64" width="32" height="32"><rect/></svg>');
  const root = {
    querySelectorAll: (sel) => (sel === 'svg.ic' ? [glyph] : sel === 'svg.draw-mode-icon' ? [face] : []),
    querySelector: (sel) => (sel === 'svg.app-logo' ? logo : null),
  };
  setPixelIcons(true);
  swapIconArt(root, true);
  assert.ok(glyph.classList.has('ic-check') && glyph.classList.has('accent-check') && glyph.classList.has('ic-sw3'));
  assert.equal(glyph.getAttribute('width'), '13');
  assert.equal(glyph.getAttribute('viewBox'), '0 0 16 16');
  assert.ok(face.classList.has('ic-draw-mode-rect'), 'the rect face stays a rect');
  assert.equal(logo.getAttribute('viewBox'), '0 0 16 16');
  assert.ok(logo.innerHTML.includes('<rect'));
  setPixelIcons(false);
  swapIconArt(root, false);
  assert.equal(glyph.getAttribute('viewBox'), '0 0 24 24');
  assert.equal(glyph.getAttribute('stroke-width'), '3', 'the line-art\'s own weight');
  assert.ok(!glyph.classList.has('ic-sw3') && glyph.classList.has('accent-check'));
  assert.match(face.html, /currentColor/);
  assert.equal(logo.getAttribute('viewBox'), '0 0 64 64');
  assert.equal(logo.innerHTML, '<rect/>');
});
