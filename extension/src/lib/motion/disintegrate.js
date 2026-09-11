// ── Disintegration ("the snap") ─────────────────────────────────────────────
// A removed element comes apart into MOTES: one round speck per grid cell in the
// element's own colours (speckPainter), drifting off in a staggered sweep. Never clones
// of the element — hundreds of copies of a row's subtree show nothing a speck does not.
// Mirror of browser motion.js; the layer is FIXED because the row collapses under it.
import { startCloud, resolveColour, paletteCss } from '../dustCloud.js';
import { dustEnabled } from '../motionPrefs.js';
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
  // Every element-sized cloud is built here, so this is where the mode turns particles
  // off: a `false` return leaves the caller on its own CSS entrance.
  if (!dustEnabled()) return false;
  try {
    cancelDust(el);   // one cloud per element: the newest gesture owns it
    const r = el.getBoundingClientRect();
    if (r.width < 4 || r.height < 4) return false;
    ({ cols, rows } = reshapeGrid(cols, rows, r.width, r.height, px));
    // Specks in the element's own colours unless the caller brought a recipe.
    const paint = paintTile || speckPainter(el);
    const host = document.createElement('div');
    host.className = hostClass ? `disintegrate-host ${hostClass}` : 'disintegrate-host';
    // Decoration, and nothing but: the layer must never take a click or a Tab stop.
    host.setAttribute('aria-hidden', 'true');
    host.inert = true;
    const span = ms || DISINTEGRATE_MS;
    // A surface flies on its own (shorter) clock; a row keeps the defaults. A ROW gather
    // on its own clock keeps the default's proportions, so the last mote to set off still
    // lands before the veil lifts.
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
    // Every grain computed once: home, throw and bend (tileMotion / surfaceMotion),
    // colour, size, clock. ONE canvas then evaluates these per frame (dustCloud.js) — no
    // node per mote, so a dialog-sized cloud costs batched fills, not hundreds of layers.
    const motes = [];
    for (let cy = 0; cy < rows; cy++) {
      for (let cx = 0; cx < cols; cx++) {
        // A row FALLS (tileMotion); a surface flies at the control that owns it.
        const m = toward
          ? surfaceMotion(cx, cy, cols, rows, r, toward, { span, spread })
          : tileMotion(cx, cy, cols, rows, gather, span);
        // The speck's size, opacity and glint; its colour comes from the palette below.
        const speck = paint({ cx, cy, cols, rows, cellW, cellH });
        // The sweep is INSIDE the span, never added to it: a late mote flies the window
        // it has left, so the whole cloud is done at `span` with no stragglers.
        motes.push({
          x: r.left + (cx + 0.5) * cellW, y: r.top + (cy + 0.5) * cellH,
          dx: m.dx, dy: m.dy, mx: m.mx, my: m.my, r: speck.px / 2, s: m.scale, a: speck.alpha,
          delay: m.delay, dur: gather ? gatherMs : Math.max(MIN_TILE_MS, span - m.delay),
          // …and its own hash for the wobble, the twinkle and its place in the palette
          // (dustCloud.js turbulenceAt / dustMix).
          w: tileNoise(cx + 13, cy + 71), t: 1, g: speck.glint ? 1 : 0,
        });
      }
    }
    const kind = flightOf(toward, gather);
    // Every cloud is painted from the theme's palette, never in the surface's own colours:
    // each grain picks its stop by its mix and its tint (dustCloud.js stopOfTint).
    const style = styleCode();
    const paints = paletteCss();
    host.__cloud = { motes, colours: paints, flight: kind, span, style };   // what a test reads
    // The element's own PARENT, not <body>: a row's cloud is torn down with its list, and
    // position:fixed still escapes the scroller's clipping. A SURFACE goes on <body> —
    // its parent (a dialog backdrop) is about to be removed under it. `hostEl` is the chat
    // entry's middle ground: outside the transcript nothing that walks it meets the layer.
    (toBody ? document.body : (hostEl || el.parentElement || document.body)).appendChild(host);
    // …but only if that parent can host it: an ancestor with a transform/filter becomes
    // the containing block for position:fixed, re-anchoring the layer and letting
    // overflow:hidden clip it. Detected by MEASURING; if it missed, re-home on <body>.
    const got = host.getBoundingClientRect();
    if (Math.abs(got.left - r.left) > 1 || Math.abs(got.top - r.top) > 1) {
      document.body.appendChild(host);
    }
    // Colours resolved ONCE per cloud, after the host is in the document — a `var(--…)`
    // needs the page's own scope to mean anything.
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

// ── Reintegration: the snap played backwards (browser motion.js twin) ───────
// Every mote starts where the scatter would have flung it and flies HOME (stTileGather in
// animations.css), sweep reversed so the first mote out is the last one in.
export const reintegrate = (el, opts = {}) => disintegrate(el, { ...opts, gather: true });
