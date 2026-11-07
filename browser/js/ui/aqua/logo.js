// The header mark under the aqua skin (config/svgArt.json aquaLogo): the gel panel swapped in
// over the app's own art, which is kept and put back when the skin goes; the gel wears the accent.
// Desktop twin: ThemePainter::makeLogoPixmap; extension twin: lib/aqua/rules.js.
import SVG_ART from '../../../../common/config/svgArt.json' with { type: 'json' };
import { gelTokens } from './rules.js';
import { swapLogoArt } from '../skin/glyphs.js';

export const AQUA_LOGO = SVG_ART.aquaLogo;
const GEL = Object.freeze(['--aq-gel-hi', '--aq-gel-top', '--aq-gel-bottom', '--aq-gel-edge']);

// `art` with its %1–%4 filled from the gel `accent` paints.
export const aquaLogoArt = (art, accent, dark = false) => {
  const gel = gelTokens(accent, dark);
  return GEL.reduce((s, name, i) => s.replaceAll(`%${i + 1}`, gel[name]), art);
};

export const swapAquaLogo = (root, on, accent = null, dark = false) =>
  swapLogoArt(root, on, on ? (logo) => { logo.innerHTML = aquaLogoArt(AQUA_LOGO.inner, accent, dark); } : null);

// The favicon wears the same mark in the accent that asks for it.
export const aquaFavicon = (hex) => aquaLogoArt(AQUA_LOGO.svg, hex);
