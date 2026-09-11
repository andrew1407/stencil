import { dustEnabled } from '../motionPrefs.js';
import { startCloud, resolveColour, paletteCss } from '../dustCloud.js';
import { speckPainter } from './painters.js';
import { SURFACE_SPREAD, surfaceMotion } from './surfaceMotion.js';
import { DISINTEGRATE_COLS, DISINTEGRATE_MS, DISINTEGRATE_ROWS, MIN_TILE_MS, MOTE_PX, TILE_GATHER_SHARE, cancelDust, flightOf, reshapeGrid, tileMotion, tileNoise } from './tiles.js';
import { styleCode } from './tune.js';
export function disintegrate(el, { cols = DISINTEGRATE_COLS, rows = DISINTEGRATE_ROWS,
                                   gather = false, toward = null, ms = 0, px = MOTE_PX,
                                   spread = SURFACE_SPREAD, drift = 1, toBody = false,
                                   hostClass = '', paintTile = null, own = true, box = null,
                                   delayScale = null, flight = null } = {}) {
  if (typeof document === 'undefined' || !el?.getBoundingClientRect || !document.body) return false;
  // Every element-sized cloud in the app is built here, so this is where the motion
  // mode turns particles off: saying no leaves the caller on its own CSS entrance
  // (that is what its `false` return has always meant).
  if (!dustEnabled()) return false;
  try {
    // One cloud per element: the newest gesture owns it. `own: false` opts a flight out of
    // that bookkeeping — an in-place VALUE swap plays two clouds over one element (the old
    // mark leaving, the new one forming) and the second must not cancel the first.
    if (own) cancelDust(el);
    // `box` is for a surface that is not AT the box it dusts over: a folding one is still
    // collapsed the moment its reveal is toggled, so the caller measures the box it is
    // about to take (foldBox) and hands it in. Everything else measures live.
    const r = box || el.getBoundingClientRect();
    if (r.width < 4 || r.height < 4) return false;
    ({ cols, rows } = reshapeGrid(cols, rows, r.width, r.height, px));
    // Specks in the element's own colours unless the caller brought a recipe (a mark
    // whose ink has already left it — markOut's `paint`).
    const paint = paintTile || speckPainter(el);
    const host = document.createElement('div');
    host.className = hostClass ? `disintegrate-host ${hostClass}` : 'disintegrate-host';
    // Decoration, and nothing but: the layer must never take a click or a Tab stop.
    host.setAttribute?.('aria-hidden', 'true');
    host.inert = true;
    // Which WINDOW the cloud came out of. A row's cloud is torn down with the list it
    // hangs in, but a cloud on <body> outlives it: deleting a project and closing the
    // window left its motes flying over the page with nothing to belong to (user report).
    // sweepDust() reads this on close. Purely a label — nothing else looks at it.
    const scope = el.closest?.('.app-modal-overlay, [data-dust-scope]');
    if (scope?.id) host.dataset.dustScope = scope.id;
    const span = ms || DISINTEGRATE_MS;
    // A surface flies on its own (shorter) clock; a row keeps the defaults. A ROW gather
    // on its own clock (a chat entry) keeps the default's proportions: the grain's
    // flight is the span less the reversed sweep (0.48s of 0.9s), so the last mote to
    // set off still lands before the veil lifts.
    const gatherMs = toward || !gather ? span : Math.round(span * TILE_GATHER_SHARE);
    if (ms) {
      host.style.setProperty('--dust-ms', `${ms}ms`);
      host.style.setProperty('--gather-ms', `${gatherMs}ms`);
      // …and the HOST lives the WHOLE span, whatever leg its grains fly: a gather's set
      // off across the reversed sweep, so the last lands at `ms`, and a host fading on
      // the leg alone took every late mote off the screen at half time.
      host.style.setProperty('--host-ms', `${ms}ms`);
    }
    host.style.left = `${r.left}px`;
    host.style.top = `${r.top}px`;
    host.style.width = `${r.width}px`;
    host.style.height = `${r.height}px`;
    const cellW = r.width / cols;
    const cellH = r.height / rows;
    // Every grain, computed once: its home (the cell's centre), its throw and bend
    // (tileMotion / surfaceMotion), its colour, size and clock. The cloud is then ONE
    // canvas evaluating these per frame (dustCloud.js) — no node per mote, so a
    // window-sized cloud costs a few batched fills rather than hundreds of layers.
    const motes = [];
    for (let cy = 0; cy < rows; cy++) {
      for (let cx = 0; cx < cols; cx++) {
        // A row FALLS (tileMotion); a surface flies at the control that owns it. The
        // scatter's sweep is halved (delayScale), and a caller may compress it further:
        // stragglers starting after the rest have gone read as a long, thin tail.
        const m = toward
          ? surfaceMotion(cx, cy, cols, rows, r, toward,
                          { span, spread, delayScale: delayScale ?? (gather ? 1 : 0.5) })
          : tileMotion(cx, cy, cols, rows, gather, drift, span);
        // The speck's size, opacity and glint; its colour comes from the palette below.
        const speck = paint({ cx, cy, cols, rows, cellW, cellH });
        // The sweep is INSIDE the span, never added to it (the desktop overlay's
        // `t = (t - delay) / (1 - delay)`): a late mote flies the window it has left, so
        // the whole cloud is done at `span` instead of trailing a quarter-second of
        // stragglers past it. Gathers already fit — their flight is the short gather
        // clock and the sweep is what fills the rest.
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
    const kind = flightOf(toward, gather, flight);
    // Every cloud is painted from the theme's palette, never in the surface's own colours:
    // each grain picks its stop by its mix and its tint (dustCloud.js stopOfTint).
    const style = styleCode();
    const paints = paletteCss();
    host.__cloud = { motes, colours: paints, flight: kind, span, style };   // what a test reads
    // Appended to the element's own PARENT, not <body>: a row's cloud is torn down with
    // the list it belongs to. Still position:fixed, so viewport-anchored, clear of
    // scroller clipping. A SURFACE goes on <body> outright: its own parent (a modal
    // overlay) is about to go display:none under it.
    (toBody ? document.body : (el.parentElement || document.body)).appendChild(host);
    // …but only if the parent can actually host it: an ancestor with a transform,
    // filter or backdrop-filter becomes the containing block for position:fixed and can
    // re-anchor or clip the layer. Detected by measuring, not by guessing which
    // properties are in play — if the layer did not land where told, re-home on <body>.
    // Skipped for a SURFACE cloud: `toBody` already put it straight on <body>.
    if (!toBody) {
      const got = host.getBoundingClientRect();
      if (Math.abs(got.left - r.left) > 1 || Math.abs(got.top - r.top) > 1) {
        document.body.appendChild(host);
      }
    }
    // Colours resolved ONCE per cloud through one probe, after the host is in the
    // document (a `var(--…)` needs the page's own scope to mean anything).
    const probe = document.createElement('span');
    host.appendChild(probe);
    const fills = paints.map((css) => resolveColour(document, css, probe));
    probe.remove();
    startCloud(host, motes, { flight: kind, span, colours: fills, origin: { x: r.left, y: r.top }, style });
    // …and the layer goes one beat after the last mote lands (the flight ends AT the
    // span now, sweep included), not most of a second later.
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

// ── Reintegration: the snap played backwards ────────────────────────────────
// The same tile layer as disintegrate, but every mote starts where the scatter would
// have flung it and flies HOME (tileGather in animations.css), with the sweep reversed
// so the first mote out is the last one in. Used by materialize below.
export const reintegrate = (el, opts = {}) => disintegrate(el, { ...opts, gather: true });
