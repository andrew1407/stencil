// The accent helpers the ported stage painter asks for (browser js/core/settings/accents.js), over the
// pre-paint window.StencilKit, and the page's accent and face as the ports read them. Under a skin
// the favicon is the skin's own mark, as the browser's setFaviconArt makes it.
import { pixelIconSvg } from '../webcore/icons.js';
import { aquaFavicon } from '../aqua/rules.js';
import { normalizeHex } from '../accent/ink.js';

export { normalizeHex };

const kit = () => globalThis.StencilKit || null;
const root = () => globalThis.document?.documentElement;

export const accentHex = (key) => kit()?.hexOf?.(key) ?? null;

export const pageDark = (el = root()) => el?.getAttribute?.('data-theme') === 'dark';
export const pageFace = (el = root()) => (pageDark(el) ? 'dark' : 'light');

// Each skin's favicon in `hex`; without a skin the kit's own mark.
const SKIN_FAVICONS = Object.freeze({
  webcore: (hex) => pixelIconSvg('logo', { accent: hex, dark: pageDark() }, 16),
  aqua: (hex) => aquaFavicon(hex),
});
export const faviconSvg = (hex) => {
  const skinned = SKIN_FAVICONS[root()?.getAttribute('data-skin')];
  return skinned ? skinned(hex) : kit()?.faviconSvg?.(hex) ?? '';
};

// `accent` is the preset on screen (a hover preview included), `customAccent` a page-only hex.
export const pageApp = () => ({
  get accent() { return root()?.getAttribute('data-accent') || globalThis.StencilAccent?.get?.() || null; },
  get customAccent() { return normalizeHex(root()?.style.getPropertyValue('--accent') || ''); },
});

// The hex the page's accent paints in: the custom one, else the preset's.
export const pageAccentHex = (app = pageApp()) => app.customAccent || accentHex(app.accent);
