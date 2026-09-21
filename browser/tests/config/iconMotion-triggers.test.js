// Per-glyph hover designs, part two: the picture frame, the assistant, trash and folder, a greyed
// control, share — plus the trigger and porting notes recorded beside the table.
import { test } from 'node:test';
import assert from 'node:assert/strict';

import ICONS from '../../js/config/icons.json' with { type: 'json' };
import MOTION from '../../js/config/iconMotion.json' with { type: 'json' };
import { ANIMATIONS_CSS } from '../helpers/css.js';

const css = ANIMATIONS_CSS;
const SECTION = (() => {
  const from = css.indexOf('/* ── Icon hover: every glyph mimes its own action');
  const to = css.indexOf('/* ── App logo hover');
  assert.ok(from >= 0 && to > from, 'the icon-motion section is where the tests expect it');
  return css.slice(from, to);
})();

test('the picture draws itself INSIDE its frame, which never moves', () => {
  const d = MOTION.icons.image;
  assert.deepEqual(d.parts.map((p) => p.hook), ['ic-ridge', 'ic-orb']);
  // The frame rect carries no hook at all, so nothing can move it — and nothing travels
  // across its outline any more, which is what sliding the contents up from below did.
  assert.ok(ICONS.image.startsWith('<rect x="3" y="3" width="18" height="18" rx="2"/>'));
  const [ridge, orb] = d.parts;
  assert.ok(ridge.dashArray >= 23, 'the ridge is ≈22.6 units of polyline');
  assert.equal(ridge.keyframes[0].dashOffset, ridge.dashArray);
  assert.equal(ridge.keyframes.at(-1).dashOffset, 0);
  // The little sun DROPS in from above, a beat after the ridge, and lands in the glyph.
  assert.ok(orb.delayMs > 0, 'the sun arrives after the ridge has started');
  assert.ok(orb.keyframes[0].translate[1] < 0, 'it starts ABOVE its place');
  assert.equal(orb.keyframes.at(-1).translate, undefined, '…and settles into it');
  for (const k of orb.keyframes) assert.equal(k.scale, undefined, 'by movement, not by scale');
  // Both are stroked at sw=2 (ui/icons.js), so the sun's visual top is cy-r-1 and the frame's outline
  // reaches to y+1: that gap is the ENTIRE travel budget, and every keyframe stays inside it.
  const half = 1;
  const y = Number(ICONS.image.match(/<rect[^>]*\by="([\d.]+)"/)[1]);
  const [cy, r] = [/\bcy="([\d.]+)"/, /\br="([\d.]+)"/].map((re) => Number(ICONS.image.match(re)[1]));
  const clearance = (cy - r - half) - (y + half);
  assert.equal(clearance, 2, 'the frame leaves the sun 2 units of headroom, not 4');
  for (const k of orb.keyframes) {
    const lift = -(k.translate?.[1] ?? 0);
    assert.ok(lift < clearance,
      `a keyframe lifts the sun ${lift}u into a ${clearance}u gap — it would cross the frame`);
  }
});

test('the assistant types and the layers assemble', () => {
  const dots = MOTION.icons.sparkle.parts[0];
  assert.equal(dots.hook, 'ic-dot');
  assert.ok(dots.stagger > 0, 'the three dots move in sequence — the typing idiom');
  assert.equal((ICONS.sparkle.match(/class="ic-dot"/g) || []).length, 3);
  // Projects: bottom layer first, top last — the stack building itself.
  const layers = MOTION.icons.layers.parts;
  assert.deepEqual(layers.map((p) => p.hook), ['ic-layer-bot', 'ic-layer-mid', 'ic-layer-top']);
  assert.ok(layers[0].delayMs === undefined && layers[1].delayMs < layers[2].delayMs);
  assert.ok(layers[0].keyframes[0].translate[1] > 0, 'the lower layers fly UP into place');
  assert.ok(layers[2].keyframes[0].translate[1] < 0, 'the top one drops onto them');
});

test('the trash lid opens on a hinge, and the folder tips open', () => {
  const lid = MOTION.icons.trash.parts[0];
  assert.equal(lid.hook, 'ic-lid');
  assert.deepEqual(lid.origin, [5, 6], 'hinged at the left end of the rim');
  // SVG's y axis points down, so a NEGATIVE angle turns the rim counter-clockwise: the far end (x=21)
  // rises off the can while the hinged stub stays put.
  assert.ok(lid.to.rotate < 0, 'and it lifts');
  assert.match(SECTION, /\.ic-trash \.ic-lid \{[^}]*transform-origin: 5px 6px;/);
  // The lid leaving the 24-unit box for a moment is why the glyphs stop clipping.
  assert.match(SECTION, /overflow: visible;/);
  assert.equal(MOTION.icons.folder.parts[0].to.rotateX < 0, true, 'the folder opens towards you');
  assert.ok(MOTION.icons.folder.parts[0].qtFallback, 'with a flat fallback for Qt');
});

test('a greyed control still reacts: the trigger does not exclude :disabled', () => {
  // It is still hovered, still shows its tooltip and its disabled reason (layout.css
  // keeps pointer-events on it for that), and the glyph is what the pointer is on.
  const trigger = SECTION.slice(SECTION.indexOf(':is(button, .btn-icon'));
  const rule = trigger.slice(0, trigger.indexOf('{'));
  assert.doesNotMatch(rule, /:not\(:disabled\)/, 'a disabled control animates like any other');
  assert.match(rule, /:not\(\.is-loading\):not\(\.swapping\)/, 'the real opt-outs stay');
  assert.match(MOTION.trigger.disabled, /^INCLUDED\./);
  assert.ok(!MOTION.trigger.excluded.some((e) => /:disabled/.test(e)));
  // …and the desktop port is told the same thing, since Qt gates on isEnabled() by hand.
  assert.match(MOTION.trigger.disabled, /isEnabled/);
});

test('share lights every node it has, top to bottom', () => {
  // Two of three circles pulsing read as one being broken, so all three play — top to
  // bottom, a plain downward sweep.
  assert.equal((ICONS.share.match(/class="ic-node"/g) || []).length, 3);
  assert.equal((ICONS.share.match(/<circle /g) || []).length, 3, 'every circle is a node');
  assert.match(ICONS.share, /^<circle class="ic-node" cx="18" cy="5"/, 'drawn top-to-bottom');
  const part = MOTION.icons.share.parts[0];
  assert.equal(part.hook, 'ic-node');
  assert.ok(part.stagger > 0, 'they light in turn, not together');
  // Markup order IS the sequence — the desktop staggers on it (support/iconMotion.hpp),
  // so the sheet's nth-of-type delays have to be that same order.
  assert.match(SECTION, /\.ic-share \.ic-node:nth-of-type\(2\) \{ --ic-delay: 0\.12s; \}/);
  assert.match(SECTION, /\.ic-share \.ic-node:nth-of-type\(3\) \{ --ic-delay: 0\.24s; \}/);
});

test('the reduced-motion contract is recorded next to the design', () => {
  assert.match(MOTION.trigger.reducedMotion, /prefers-reduced-motion/);
  assert.match(MOTION.trigger.layout, /Transform/);
  for (const key of ['on', 'excluded']) assert.ok(MOTION.trigger[key]);
  // The port instructions travel WITH the design, including the accessibility rule the
  // desktop's shimmer/logo/spin helpers currently miss.
  assert.match(MOTION.portingNotes.qtReducedMotion, /motionReduced|STENCIL_NO_ANIM/);
  for (const key of ['qt', 'extension']) assert.ok(MOTION.portingNotes[key]);
  // The fold chevrons' rotation is state, not hover feedback: they are excluded here
  // and by the [id^="toggle-"] guard in the sheet.
  assert.ok(MOTION.trigger.excluded.some((e) => /toggle-/.test(e)));
  assert.match(SECTION, /:not\(\[id\^="toggle-"\]\)/);
});
