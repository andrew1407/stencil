// The logo stage's table (config/logoStage.json) and the pure rules over it: which show a hold
// opens for an accent + motion mode, the cloud style a show wears, the stage sizes and the heart
// the pink show draws. DOM-free; desktop twin: support/logoStageRules.{hpp,cpp}, value for value.
import LOGO_STAGE from '../../config/logoStage.json' with { type: 'json' };

export const STAGE = LOGO_STAGE;
export const SHOWS = Object.freeze(LOGO_STAGE.shows);
export const SHOW_NAMES = Object.freeze(Object.keys(SHOWS));
export const TYPED_WORDS = Object.freeze(SHOW_NAMES.map((n) => n.toLowerCase()));
export const HOLD_MS = LOGO_STAGE.holdMs;
export const TOAST_TEXT = LOGO_STAGE.toast;
// The shows whose mark roams the window, wearing whatever cloud the motion mode gives.
const ROAMING = new Set(['follow', 'escape', 'fly']);

export const effectOf = (name) => SHOWS[name]?.effect ?? null;

// A custom accent picks by its hex ('*' = every other one); a preset by its row, and a row with
// a `motion` opens only under that motion mode. null = the hold does nothing.
export const resolveShow = (accentKey, customHex, motionMode) => {
  const hex = typeof customHex === 'string' ? customHex.trim().toLowerCase() : '';
  if (hex) {
    return SHOW_NAMES.find((n) => SHOWS[n].customHex === hex)
      ?? SHOW_NAMES.find((n) => SHOWS[n].customHex === '*') ?? null;
  }
  const name = SHOW_NAMES.find((n) => (SHOWS[n].accents || []).includes(accentKey));
  if (!name) return null;
  const need = SHOWS[name].motion;
  return !need || need === motionMode ? name : null;
};

// 'dust' | 'water' | 'fire' | null: a styled show forces its own cloud, a roaming one takes the
// current style (dustCloud.js PARTICLE_STYLES keys), the rest fly none.
// A show with a motion of its OWN wears it; every other one wears whatever particle style the
// user is running, and with particles off there is no cloud and the light does the whole show.
// NEON is that light and SUN its own ring of beams, so neither ever wears a cloud.
const LIGHT_ONLY = new Set(['neon', 'sun']);
export const showStyle = (name, currentStyle = null) => {
  const show = SHOWS[name];
  if (!show) return null;
  if (show.motion) return show.effect;
  return LIGHT_ONLY.has(show.effect) ? null : currentStyle;
};

export const bigLogoSize = (w, h) => Math.round(Math.min(w, h) * STAGE.stage.logoShare);
// A BOUNCING show (shrink, grow) travels the whole range, so its big end fills the window
// rather than sitting at the still shows' resting size.
export const bounceBigSize = (w, h) => Math.round(Math.min(w, h) * STAGE.stage.bounceBigShare);
export const bounces = (name) => effectOf(name) === 'shrink' || effectOf(name) === 'grow';
// A show that ROAMS wears a small mark: it is a thing moving over the window, not a backdrop.
export const roams = (name) => ROAMING.has(effectOf(name));
export const roamLogoSize = (w, h) => Math.round(Math.min(w, h) * STAGE.stage.roamShare);
export const minLogoSize = (w, h) => Math.round(Math.min(w, h) * STAGE.stage.minShare);

// Where a ray at `angle` from the centre meets the mark's own outline — the PAINTED panel, which
// is inset inside the icon box and rounded (svgArt.json favicon: 60 of 64 wide, rx 13). A cloud is
// born along it, so the grains hug the art: a circle buries the four corners, the icon box leaves
// a bare margin outside every edge, and a sharp square leaves a crescent outside each corner.
export const markEdge = (size, angle) => {
  const c = Math.cos(angle), s = Math.sin(angle);
  const half = size * STAGE.stage.markEdgeShare, r = Math.min(size * STAGE.stage.markCornerShare, half);
  const flat = half - r;                       // half-extents of the square the corners round off
  const ac = Math.abs(c), as = Math.abs(s);
  let t;
  if (ac > 0 && (half / ac) * as <= flat) t = half / ac;        // out through a vertical side
  else if (as > 0 && (half / as) * ac <= flat) t = half / as;   // out through a horizontal one
  else {
    // Out through a corner: the ray meets the arc of radius r centred on (flat, flat).
    const k = ac * flat + as * flat;
    t = k + Math.sqrt(Math.max(0, k * k - (2 * flat * flat - r * r)));
  }
  return { x: c * t, y: s * t };
};

const round2 = (v) => Math.round(v * 100) / 100;

// The heart, x = 16 sin³t, y = 13 cos t − 5 cos 2t − 2 cos 3t − cos 4t, fitted to the centred
// square of a w×h image (y down), inset from its edges — n points round, a closed shape once
// the line is locked.
export const heartPoints = (w, h, n = STAGE.pink.heartPoints) => {
  const raw = [];
  for (let i = 0; i < n; i++) {
    const t = (2 * Math.PI * i) / n;
    raw.push([16 * Math.sin(t) ** 3,
      -(13 * Math.cos(t) - 5 * Math.cos(2 * t) - 2 * Math.cos(3 * t) - Math.cos(4 * t))]);
  }
  let minX = Infinity, maxX = -Infinity, minY = Infinity, maxY = -Infinity;
  for (const [x, y] of raw) {
    minX = Math.min(minX, x); maxX = Math.max(maxX, x);
    minY = Math.min(minY, y); maxY = Math.max(maxY, y);
  }
  const k = (Math.min(w, h) * (1 - 2 * STAGE.pink.heartInsetShare)) / Math.max(maxX - minX, maxY - minY);
  const bx = (minX + maxX) / 2, by = (minY + maxY) / 2;
  return raw.map(([x, y]) => ({ x: round2(w / 2 + (x - bx) * k), y: round2(h / 2 + (y - by) * k) }));
};

// The pink show's line, as the layout install takes it.
export const heartLine = (w, h) => ({
  points: heartPoints(w, h),
  locked: true,
  color: STAGE.pink.heartStroke,
  fillColor: STAGE.pink.heartFill,
  thickness: STAGE.pink.heartThickness,
});
