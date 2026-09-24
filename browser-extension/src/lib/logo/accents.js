// The three accent helpers the ported stage painter asks for (browser js/core/settings/accents.js),
// over the pre-paint window.StencilKit, and the page's accent as the `app` the ports read.
// Under the webcore skin the mark is the pixel one, as the browser's setFaviconArt makes it.
import { pixelIconSvg } from '../webcore/icons.js';

export const normalizeHex = (value) => {
  if (typeof value !== 'string') return null;
  let h = value.trim().replace(/^#/, '');
  if (/^[0-9a-fA-F]{3}$/.test(h)) h = h.split('').map((c) => c + c).join('');
  return /^[0-9a-fA-F]{6}$/.test(h) ? '#' + h.toLowerCase() : null;
};

const kit = () => globalThis.StencilKit || null;
const root = () => globalThis.document?.documentElement;

export const accentHex = (key) => kit()?.hexOf?.(key) ?? null;

export const faviconSvg = (hex) => {
  const el = root();
  if (el?.getAttribute('data-skin') === 'webcore')
    return pixelIconSvg('logo', { accent: hex, dark: el.getAttribute('data-theme') === 'dark' }, 16);
  return kit()?.faviconSvg?.(hex) ?? '';
};

// `accent` is the preset on screen (a hover preview included), `customAccent` a page-only hex.
export const pageApp = () => ({
  get accent() { return root()?.getAttribute('data-accent') || globalThis.StencilAccent?.get?.() || null; },
  get customAccent() { return normalizeHex(root()?.style.getPropertyValue('--accent') || ''); },
});

// The hex the page's accent paints in: the custom one, else the preset's.
export const pageAccentHex = (app = pageApp()) => app.customAccent || accentHex(app.accent);
