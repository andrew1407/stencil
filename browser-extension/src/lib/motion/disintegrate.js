// ── Disintegration ("the snap") ─────────────────────────────────────────────
// A removed element comes apart into motes in its own colours (speckPainter), never clones.
// Mirror of browser motion.js; the layer is fixed because the row collapses under it.
import { startCloud, resolveColour, paletteCss } from '../dust/cloud.js';
import { dustEnabled } from '../prefs/motionPrefs.js';
import { speckPainter } from './painters.js';
import { SURFACE_SPREAD, surfaceMotion } from './surfaceMotion.js';
import { DISINTEGRATE_COLS, DISINTEGRATE_MS, DISINTEGRATE_ROWS, MIN_TILE_MS, MOTE_PX,
         TILE_GATHER_SHARE, cancelDust, flightOf, reshapeGrid, tileMotion, tileNoise } from './tiles.js';
import { styleCode } from './tune.js';

export function disintegrate(el, { cols = DISINTEGRATE_COLS, rows = DISINTEGRATE_ROWS,
                                   gather = false, toward = null, ms = 0, px = MOTE_PX,
                                   spread = SURFACE_SPREAD, toBody = false, hostEl = null,
                                   hostClass = '', paintTile = null } = {}) {
  if (typeof document === 'undefined' || !el?.getBoundingClientRect || !document.body) return false;
  // The mode turns particles off here: `false` leaves the caller on its own CSS entrance.
  if (!dustEnabled()) return false;
  try {
    cancelDust(el);   // one cloud per element: the newest gesture owns it
    const r = el.getBoundingClientRect();
    if (r.width < 4 || r.height < 4) return false;
    ({ cols, rows } = reshapeGrid(cols, rows, r.width, r.height, px));
    const paint = paintTile || speckPainter(el);
    const host = document.createElement('div');
    host.className = hostClass ? `disintegrate-host ${hostClass}` : 'disintegrate-host';
    host.setAttribute('aria-hidden', 'true');
    host.inert = true;
    const span = ms || DISINTEGRATE_MS;
    // A surface flies on its own shorter clock; the gather keeps the default's proportions so the
    // last mote lands before the veil lifts.
    const gatherMs = toward || !gather ? span : Math.round(span * TILE_GATHER_SHARE);
    if (ms) {
      host.style.setProperty('--dust-ms', `${ms}ms`);
      host.style.setProperty('--gather-ms', `${gatherMs}ms`);
    }
    host.style.left = `${r.left}px`;
    host.style.top = `${r.top}px`;
    host.style.width = `${r.width}px`;
    host.style.height = `${r.height}px`;
    const cellW = r.width / cols;
    const cellH = r.height / rows;
    // One canvas evaluates every grain per frame (cloud.js), not a node per mote.
    const motes = [];
    for (let cy = 0; cy < rows; cy++) {
      for (let cx = 0; cx < cols; cx++) {
        const m = toward
          ? surfaceMotion(cx, cy, cols, rows, r, toward, { span, spread })
          : tileMotion(cx, cy, cols, rows, gather, span);
        const speck = paint({ cx, cy, cols, rows, cellW, cellH });
        // The sweep is inside the span, so the whole cloud is done at `span`.
        motes.push({
          x: r.left + (cx + 0.5) * cellW, y: r.top + (cy + 0.5) * cellH,
          dx: m.dx, dy: m.dy, mx: m.mx, my: m.my, r: speck.px / 2, s: m.scale, a: speck.alpha,
          delay: m.delay, dur: gather ? gatherMs : Math.max(MIN_TILE_MS, span - m.delay),
          w: tileNoise(cx + 13, cy + 71), t: 1, g: speck.glint ? 1 : 0,
        });
      }
    }
    const kind = flightOf(toward, gather);
    // Painted from the theme's palette, never the surface's own colours (cloud.js stopOfTint).
    const style = styleCode();
    const paints = paletteCss();
    host.__cloud = { motes, colours: paints, flight: kind, span, style };   // what a test reads
    // The element's parent, so a row's cloud goes with its list; a surface goes on <body>.
    (toBody ? document.body : (hostEl || el.parentElement || document.body)).appendChild(host);
    // A transformed ancestor contains position:fixed, so the landing is measured and re-homed.
    const got = host.getBoundingClientRect();
    if (Math.abs(got.left - r.left) > 1 || Math.abs(got.top - r.top) > 1) {
      document.body.appendChild(host);
    }
    // Resolved once the host is in the document, where a `var(--…)` has its scope.
    const probe = document.createElement('span');
    host.appendChild(probe);
    const fills = paints.map((css) => resolveColour(document, css, probe));
    probe.remove();
    startCloud(host, motes, { flight: kind, span, colours: fills, origin: { x: r.left, y: r.top }, style });
    el.__dustHost = host;
    el.__dustTimer = setTimeout(() => {
      host.__stop?.();
      host.remove();
      if (el.__dustHost === host) { el.__dustHost = null; el.__dustTimer = null; }
    }, span + 400);
    return true;
  } catch {
    return false;   // decoration only — the removal carries on regardless
  }
}

// Reintegration (browser motion.js twin): every mote flies home from its scatter pose.
export const reintegrate = (el, opts = {}) => disintegrate(el, { ...opts, gather: true });
