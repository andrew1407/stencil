// The tab's favicon and theme-color in the accent: the writers over core/settings/accents.js's
// favicon art, the DOM half of the accent the core keeps pure.
import { faviconSvg, accentHex, isAccent, DEFAULT_ACCENT } from '../../core/settings/accents.js';

// The favicon is a static .svg that can't read a CSS var, so the <link> becomes an inline data URL.
export const applyFaviconHex = (hex) => {
  if (typeof document === 'undefined') return;
  let link = document.querySelector('link[rel="icon"]');
  if (!link) {
    link = document.createElement('link');
    link.rel = 'icon';
    document.head.appendChild(link);
  }
  link.type = 'image/svg+xml';
  link.href = 'data:image/svg+xml,' + encodeURIComponent(faviconSvg(hex));
  const meta = document.querySelector('meta[name="theme-color"]');
  if (meta) meta.setAttribute('content', hex);
};

export const applyAccentFavicon = (key) =>
  applyFaviconHex(accentHex(isAccent(key) ? key : DEFAULT_ACCENT));
