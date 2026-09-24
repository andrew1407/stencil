import { dustEnabled } from './motionPrefs.js';
import { startCloud, resolveColour, paletteCss } from '../dust/cloud.js';
import { speckPainter } from './surface/painters.js';
import { SURFACE_SPREAD, surfaceMotion } from './surface/motion.js';
import { DISINTEGRATE_COLS, DISINTEGRATE_MS, DISINTEGRATE_ROWS, MIN_TILE_MS, MOTE_PX, TILE_GATHER_SHARE, cancelDust, flightOf, reshapeGrid, tileMotion, tileNoise } from './surface/tiles.js';
import { styleCode } from './tune.js';
// `data-dust-scope`: the selector its window matches while open; a closed one starts no row cloud.
const DUST_SCOPE = '.app-modal-overlay, [data-dust-scope]';
const scopeOpen = (scope) => typeof scope.matches !== 'function'
  || scope.matches(scope.dataset?.dustScope || '.modal-open');

export function disintegrate(el, { cols = DISINTEGRATE_COLS, rows = DISINTEGRATE_ROWS,
                                   gather = false, toward = null, ms = 0, px = MOTE_PX,
                                   spread = SURFACE_SPREAD, drift = 1, toBody = false,
                                   hostClass = '', paintTile = null, own = true, box = null,
                                   delayScale = null, flight = null, scoped = true } = {}) {
  if (typeof document === 'undefined' || !el?.getBoundingClientRect || !document.body) return false;
// Every element-sized cloud is built here, so this is where the motion mode turns
// particles off; `false` leaves the caller on its own CSS entrance.
  if (!dustEnabled()) return false;
  try {
// One cloud per element; `own: false` lets a value swap play two clouds over one element.
    if (own) cancelDust(el);
// `box`: a folding surface is still collapsed when its reveal is toggled, so the caller
// hands in the box it is about to take (foldBox).
    const r = box || el.getBoundingClientRect();
    if (r.width < 4 || r.height < 4) return false;
    const scope = scoped ? el.closest?.(DUST_SCOPE) : null;
    if (scope && !scopeOpen(scope)) return false;
    ({ cols, rows } = reshapeGrid(cols, rows, r.width, r.height, px));
    const paint = paintTile || speckPainter(el);
    const host = document.createElement('div');
    host.className = hostClass ? `disintegrate-host ${hostClass}` : 'disintegrate-host';
    // Decoration, and nothing but: the layer must never take a click or a Tab stop.
    host.setAttribute?.('aria-hidden', 'true');
    host.inert = true;
// Which window the cloud came out of, for sweepDust() on close; a cloud on <body>
// outlives the list it hangs in.
    if (scope?.id) host.dataset.dustScope = scope.id;
    const span = ms || DISINTEGRATE_MS;
// A row gather on its own clock keeps the default's proportions (gather leg 0.48s of
// 0.9s), so the last mote still lands before the veil lifts.
    const gatherMs = toward || !gather ? span : Math.round(span * TILE_GATHER_SHARE);
    if (ms) {
      host.style.setProperty('--dust-ms', `${ms}ms`);
      host.style.setProperty('--gather-ms', `${gatherMs}ms`);
// The host lives the whole span: a gather's motes set off across the reversed sweep.
      host.style.setProperty('--host-ms', `${ms}ms`);
    }
    host.style.left = `${r.left}px`;
    host.style.top = `${r.top}px`;
    host.style.width = `${r.width}px`;
    host.style.height = `${r.height}px`;
    const cellW = r.width / cols;
    const cellH = r.height / rows;
// Every grain computed once; the cloud is one canvas evaluating these per frame
// (cloud.js) — no node per mote.
    const motes = [];
    for (let cy = 0; cy < rows; cy++) {
      for (let cx = 0; cx < cols; cx++) {
// The scatter's sweep is halved (delayScale) unless the caller says otherwise.
        const m = toward
          ? surfaceMotion(cx, cy, cols, rows, r, toward,
                          { span, spread, delayScale: delayScale ?? (gather ? 1 : 0.5) })
          : tileMotion(cx, cy, cols, rows, gather, drift, span);
        const speck = paint({ cx, cy, cols, rows, cellW, cellH });
// null is "no ink under this cell" (painters.js inkPainter): line-art flies its shape.
        if (!speck) continue;
// The sweep is inside the span, never added to it (desktop: `t = (t - delay) / (1 - delay)`),
// so the whole cloud is done at `span`.
        motes.push({
          x: r.left + (cx + 0.5) * cellW, y: r.top + (cy + 0.5) * cellH,
          dx: m.dx, dy: m.dy, mx: m.mx, my: m.my, r: speck.px / 2, s: m.scale, a: speck.alpha,
          delay: m.delay, dur: gather ? gatherMs : Math.max(MIN_TILE_MS, span - m.delay),
// Its own hash for the wobble, twinkle and palette stop (cloud.js turbulenceAt / dustMix).
          w: tileNoise(cx + 13, cy + 71), t: 1, g: speck.glint ? 1 : 0,
        });
      }
    }
    const kind = flightOf(toward, gather, flight);
// Painted from the theme's palette, never the surface's colours (cloud.js stopOfTint).
    const style = styleCode();
    const paints = paletteCss();
    host.__cloud = { motes, colours: paints, flight: kind, span, style };   // what a test reads
// The element's parent, so a row's cloud is torn down with its list; a surface goes on
// <body> because its overlay is about to go display:none.
    (toBody ? document.body : (el.parentElement || document.body)).appendChild(host);
// A transformed/filtered ancestor becomes the containing block for position:fixed;
// detected by measuring — if the layer did not land where told, re-home on <body>.
    if (!toBody) {
      const got = host.getBoundingClientRect();
      if (Math.abs(got.left - r.left) > 1 || Math.abs(got.top - r.top) > 1) {
        document.body.appendChild(host);
      }
    }
// After the host is in the document: a `var(--…)` needs the page's scope.
    const probe = document.createElement('span');
    host.appendChild(probe);
    const fills = paints.map((css) => resolveColour(document, css, probe));
    probe.remove();
    startCloud(host, motes, { flight: kind, span, colours: fills, origin: { x: r.left, y: r.top }, style });
    const life = setTimeout(() => {
      host.__stop?.();
      host.remove();
      if (el.__dustHost === host) { el.__dustHost = null; el.__dustTimer = null; }
    }, span + 150);
    if (own) { el.__dustHost = host; el.__dustTimer = life; }
    return true;
  } catch {
    return false;   // decoration only — the removal carries on regardless
  }
}

// The snap backwards: every mote starts where the scatter would fling it, sweep reversed.
export const reintegrate = (el, opts = {}) => disintegrate(el, { ...opts, gather: true });
