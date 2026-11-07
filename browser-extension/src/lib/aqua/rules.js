// The aqua skin's pure rules: the gel an accent paints (aqua.json `tint`), the gel mark in it
// (svgArt.json aquaLogo) and the glossy icons (iconsAqua.json), each copy minting its own gradient
// ids. Byte copies of common/config/ (tests/dataParity.test.js). Browser twins: js/ui/aqua/rules.js,
// logo.js and icons.js (tests/portParityFunctions.test.js).
import AQUA_DATA from './aqua.json' with { type: 'json' };
import SVG_ART from './svgArt.json' with { type: 'json' };
import ART from './iconsAqua.json' with { type: 'json' };
import { normalizeHex, onAccentInk } from '../accent/ink.js';

export const AQUA = AQUA_DATA;
export const AQUA_LOGO = SVG_ART.aquaLogo;

const channels = (hex) => [1, 3, 5].map((i) => parseInt(hex.slice(i, i + 2), 16));
// `a` moved toward `b` by `share` in sRGB, each channel rounded: what CSS color-mix(in srgb) gives.
export const mixHex = (a, b, share) => {
  const [x, y] = [channels(a), channels(b)];
  return `#${x.map((c, i) => Math.round(c * (1 - share) + y[i] * share).toString(16).padStart(2, '0')).join('')}`;
};

// The gel tokens an accent paints: with the base hex they are the table's own.
export const gelTokens = (accent, dark = false) => {
  const tint = normalizeHex(accent) || AQUA.tint.base;
  const out = {};
  for (const [name, [toward, share]] of Object.entries(AQUA.tint[dark ? 'dark' : 'light'])) out[name] = mixHex(tint, toward, share);
  out['--aq-on-select'] = onAccentInk(out['--aq-select']);
  out['--aq-on-gel'] = onAccentInk(out['--aq-gel-bottom']);   // the word on a gel capsule, read against its deep half
  return out;
};
export const GEL_NAMES = Object.freeze([...Object.keys(AQUA.tint.light), '--aq-on-select', '--aq-on-gel']);

const GEL = Object.freeze(['--aq-gel-hi', '--aq-gel-top', '--aq-gel-bottom', '--aq-gel-edge']);

// `art` with its %1–%4 filled from the gel `accent` paints.
export const aquaLogoArt = (art, accent, dark = false) => {
  const gel = gelTokens(accent, dark);
  return GEL.reduce((s, name, i) => s.replaceAll(`%${i + 1}`, gel[name]), art);
};

export const aquaFavicon = (hex) => aquaLogoArt(AQUA_LOGO.svg, hex);

let minted = 0;
// The art with its `%u` gradient ids made this copy's own.
export const aquaIconArt = (name) => {
  const art = ART.icons[name];
  return art ? art.replaceAll('%u', `aq${(minted += 1)}`) : '';
};

export const hasAquaIcon = (name) => Object.hasOwn(ART.icons, name);
