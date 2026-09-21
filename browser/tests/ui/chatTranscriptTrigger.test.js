// Placing the "…" trigger (js/ui/motion.js): bottom-anchored on --visible-bottom, inside the
// row ∩ viewport whichever edge is cut, and hidden on a slice too short to hold it.
import { test } from 'node:test';
import assert from 'node:assert';
import {
  revealVisibleBottom, revealTriggerFits, REVEAL_SMOOTH_FEATHER_PX, REVEAL_TRIGGER_SIZE,
  REVEAL_TRIGGER_PAD,
} from '../../js/ui/motion.js';

// The "…" trigger is bottom-anchored in the row, so it rides --visible-bottom (motion.js
// publishes it from the mask's own cached geometry) rather than `bottom: 0`.
const BTN = 21;   // .chat-row-menu-btn is 21x21 (pinned below)
// The button's box and the row's on-screen slice, both in row-local coordinates.
const buttonBox = (top, h, viewH) => {
  const bottom = h - revealVisibleBottom(top, top + h, viewH);
  return { top: bottom - BTN, bottom };
};
const visibleSlice = (top, h, viewH) => ({ top: Math.max(0, -top), bottom: Math.min(h, viewH - top) });
const overlap = (a, b) => Math.max(0, Math.min(a.bottom, b.bottom) - Math.max(a.top, b.top));

test('the "…" trigger lands inside the row ∩ viewport, whichever edge is cut', () => {
  const H = 500;
  const cases = [
    ['fully visible', 40, 120],
    ['clipped at the top', -120, 146],
    ['a sliver left at the bottom of the top row', -141, 146],
    ['clipped at the bottom', 430, 146],
    ['a sliver peeking in at the bottom', 495, 146],
    ['taller than the whole scroller', -50, 900],
  ];
  for (const [name, top, h] of cases) {
    const box = buttonBox(top, h, H);
    const slice = visibleSlice(top, h, H);
    assert.ok(overlap(box, slice) > 0, `${name}: some of the trigger is on screen`);
    assert.ok(box.bottom <= slice.bottom + 0.001, `${name}: it never hangs below the visible slice`);
    assert.ok(box.bottom >= slice.top, `${name}: nor floats above it`);
    // …and in the scroller's own coordinates it is inside the viewport.
    assert.ok(top + box.bottom >= 0 && top + box.bottom <= H, `${name}: inside the viewport`);
  }
  // A row nothing of which is on screen keeps the plain anchor — there is nothing to
  // clamp to, and it costs no work.
  assert.strictEqual(revealVisibleBottom(600, 720, H), 0, 'wholly below the fold');
  assert.strictEqual(revealVisibleBottom(-300, -100, H), 0, 'wholly above it');
  assert.strictEqual(revealVisibleBottom(10, 10, H), 0, 'an empty row');
});

test('revealVisibleBottom clears a CONSTANT fade band, but never leaves the visible slice', () => {
  const H = 500;
  // Nothing cut at the bottom ⇒ the trigger stays exactly on the row's own edge.
  assert.strictEqual(revealVisibleBottom(40, 160, H), 0);
  assert.strictEqual(revealVisibleBottom(-120, 26, H), 0, 'a top-clipped row still anchors at its bottom');
  // Cut at the bottom ⇒ up to the visible edge, less the fixed band the fade covers.
  // 146px row, 100px of it showing: 146 - (100 - 12).
  assert.strictEqual(revealVisibleBottom(400, 546, H), 146 - (100 - REVEAL_SMOOTH_FEATHER_PX));
  // The band is a constant, not a share of height: the pill's bottom edge stays 12px inside
  // the cut whatever the row measures.
  for (const h of [120, 400, 1200]) {
    const top = H - 60;                       // 60px of the row showing, bottom cut
    const bottomEdge = h - revealVisibleBottom(top, top + h, H);
    assert.strictEqual(bottomEdge, 60 - REVEAL_SMOOTH_FEATHER_PX, `h=${h}: a constant, not a ratio`);
  }
  // On a sliver the band is capped at half the slice — being seen beats being clear of
  // a band that is fully faded anyway (such a row hides its trigger, see below).
  assert.strictEqual(revealVisibleBottom(495, 641, H), 146 - 2.5);
});

// ── …and it is not shown at all when it cannot be placed cleanly ────────────
test('revealTriggerFits: hidden on a slice too short to hold it, always on a whole row', () => {
  const H = 500;
  const room = REVEAL_TRIGGER_SIZE + REVEAL_TRIGGER_PAD + REVEAL_SMOOTH_FEATHER_PX;   // 37
  // A row you can see in full always keeps its trigger — it sits on that row's own
  // bottom edge and touches nothing, however short the bubble is.
  assert.strictEqual(revealTriggerFits(40, 160, H), true);
  assert.strictEqual(revealTriggerFits(40, 58, H), true, 'a one-line bubble still gets one');
  // Bottom-clipped: enough of the slice to hold the pill clear of the bubble below…
  assert.strictEqual(revealTriggerFits(H - room, H - room + 400, H), true);
  // …and not a pixel less, or it would be drawn across the neighbour.
  assert.strictEqual(revealTriggerFits(H - room + 1, H - room + 401, H), false);
  // Top-clipped rows need no band, so they need less slice.
  assert.strictEqual(revealTriggerFits(-380, 20, H), false, 'a 20px sliver at the top: no room');
  assert.strictEqual(revealTriggerFits(-380, 45, H), true);
  // Hysteresis: a row already hidden needs 2px more before it comes back, so a scroll
  // grazing the threshold cannot flutter the pill on and off.
  assert.strictEqual(revealTriggerFits(H - room, H - room + 400, H, true), false);
  assert.strictEqual(revealTriggerFits(H - room - 2, H - room + 398, H, true), true);
  // Nothing on screen / an empty row: nothing to place.
  assert.strictEqual(revealTriggerFits(600, 720, H), false);
  assert.strictEqual(revealTriggerFits(10, 10, H), false);
});
