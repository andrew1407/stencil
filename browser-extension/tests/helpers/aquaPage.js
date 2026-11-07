// A page for the aqua skin suites (src/lib/aqua/skin.js): a header mark, two gear glyphs, a glyph
// the glossy table lacks and a favicon, over the stub DOM, its observers and window events fired
// by hand.
import { installDom, stubDoc, stubEl } from './domStub.js';

export const HEX = { blue: '#2563eb', red: '#dc2626', green: '#16a34a' };

export const fakePage = (skin = 'aqua', { accent = 'blue', custom = '' } = {}) => {
  const doc = stubDoc();
  const root = doc.documentElement;
  root.setAttribute('data-skin', skin);
  root.setAttribute('data-accent', accent);
  if (custom) root.style.setProperty('--accent', custom);
  const glyph = (cls, inner) => {
    const el = stubEl('svg');
    el.className = cls;
    el.classList[Symbol.iterator] = () => el.classes.values();
    el.innerHTML = inner;
    for (const [k, v] of [['viewBox', '0 0 24 24'], ['fill', 'none'], ['stroke', 'currentColor']]) el.setAttribute(k, v);
    el.hasAttribute = (k) => el.getAttribute(k) !== null;
    el.matches = () => true;
    return el;
  };
  const logo = glyph('logo', '<rect id="own"/>');
  const gears = [glyph('ic ic-gear', '<circle/>'), glyph('ic ic-gear', '<circle/>')];
  const plain = glyph('ic ic-sidebar', '<rect/>');
  doc.body.querySelectorAll = () => [logo, ...gears, plain];
  const link = { href: '' };
  doc.querySelector = (sel) => (sel === 'link[rel="icon"]' ? link : null);
  const observers = [];
  const win = stubEl('window');
  win.MutationObserver = class { constructor(cb) { this.cb = cb; } observe(target) { observers.push({ target, cb: this.cb }); } };
  win.StencilAccent = { storageKey: 'stencil_accent' };
  const fire = () => { for (const o of observers) if (o.target === root) o.cb([]); };
  const restore = installDom({ document: doc, StencilKit: { hexOf: (k) => HEX[k], faviconSvg: (hex) => `<svg plain="${hex}"/>` } });
  return { doc, root, win, logo, gears, plain, link, fire, restore };
};
