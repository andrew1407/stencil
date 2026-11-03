// The Sliding mode's window flight vars: a box leaves its opener stretched and pinched toward it,
// lands in a squash and settles (lib/animations/keyframes.css stGenieIn / stGenieOut read them).
// A port of browser/js/ui/motion/genie/vars.js, pinned by tests/portParity.test.js.
import { genieSide } from './warp.js';

export { genieSide };

// The tilt at the opener (deg): the side facing it recedes, so the box narrows toward it.
export const GENIE_TILT_DEG = 38;
// Mid-flight the box is this long along the flight and this narrow across it.
export const GENIE_STRETCH = 1.14;
export const GENIE_PINCH = 0.42;
// The landing squash, along and across: short and wide, then it springs back.
export const GENIE_SQUASH = 0.95;
export const GENIE_BULGE = 1.04;
// The perspective, in box lengths along the flight (floored in px): smaller tilts harder.
export const GENIE_DEPTH = 1.25;
// An opener over the window itself has no side to funnel toward: the box pops, tall then wide.
export const GENIE_POP = Object.freeze({ kx: 0.78, ky: 0.9, ox: 1.03, oy: 0.97 });
const MIN_DEPTH_PX = 320;
const MIN_SCALE = 0.05;

const shapeFor = (side) => {
  if (!side) return { rx: 0, ry: 0, ...GENIE_POP };
  const vertical = side === 'up' || side === 'down';
  // rotateX(+) recedes the top edge, rotateY(+) the right one: the side facing the opener goes away.
  const tilt = (side === 'up' || side === 'right') ? GENIE_TILT_DEG : -GENIE_TILT_DEG;
  return {
    rx: vertical ? tilt : 0,
    ry: vertical ? 0 : tilt,
    kx: vertical ? GENIE_PINCH : GENIE_STRETCH,
    ky: vertical ? GENIE_STRETCH : GENIE_PINCH,
    ox: vertical ? GENIE_BULGE : GENIE_SQUASH,
    oy: vertical ? GENIE_SQUASH : GENIE_BULGE,
  };
};

// `box` is the window's resting client rect, `from` the opener's (a rect) or null to fall from above.
// Returns the custom properties the flight keyframes read, or null for an unmeasured box.
export function genieVars(box, from) {
  if (!(box?.width > 0 && box?.height > 0)) return null;
  const onScreen = !!from && from.width > 0 && from.height > 0;
  const bx = box.left + box.width / 2;
  const by = box.top + box.height / 2;
  const px = onScreen ? from.left + from.width / 2 : bx;
  const py = onScreen ? from.top + from.height / 2 : -Math.max(48, box.height * 0.3);
  const side = genieSide(box, px, py);
  const shape = shapeFor(side);
  const along = side === 'left' || side === 'right' ? box.width : box.height;
  return {
    '--modal-dx': `${Math.round(px - bx)}px`,
    '--modal-dy': `${Math.round(py - by)}px`,
    '--modal-sx': String(onScreen ? Math.max(from.width / box.width, MIN_SCALE) : MIN_SCALE),
    '--modal-sy': String(onScreen ? Math.max(from.height / box.height, MIN_SCALE) : MIN_SCALE),
    '--modal-rx': `${shape.rx}deg`,
    '--modal-ry': `${shape.ry}deg`,
    '--modal-persp': `${Math.round(Math.max(MIN_DEPTH_PX, GENIE_DEPTH * along))}px`,
    '--modal-kx': String(shape.kx),
    '--modal-ky': String(shape.ky),
    '--modal-ox': String(shape.ox),
    '--modal-oy': String(shape.oy),
  };
}

export const GENIE_VAR_NAMES = Object.freeze(Object.keys(genieVars({ left: 0, top: 0, width: 1, height: 1 }, null)));

// Stamps genieVars on `el` (a stale set cleared first); false when the box is unmeasured.
export function applyGenieVars(el, box, from) {
  clearGenieVars(el);
  const vars = genieVars(box, from);
  if (!vars || !el?.style) return false;
  for (const [k, v] of Object.entries(vars)) el.style.setProperty(k, v);
  return true;
}

export function clearGenieVars(el) {
  for (const k of GENIE_VAR_NAMES) el?.style?.removeProperty(k);
}
