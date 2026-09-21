// Per-glyph hover designs (config/iconMotion.json against config/icons.json and the sheet): the
// direction pairs, plus/minus, fullscreen, sun/moon, the eraser, help and close.
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

test('direction is the meaning: the pairs point opposite ways', () => {
  const to = (name, hook) => MOTION.icons[name].parts.find((p) => p.hook === hook).to;
  assert.ok(to('download', 'ic-arrow').translate[1] > 0, 'download goes DOWN');
  assert.ok(to('upload', 'ic-arrow').translate[1] < 0, 'upload goes UP');
  // Undo and redo draw themselves, so their direction lives in the glyph: the head path runs left for
  // undo and right for redo, and the shaft starts AT the head.
  const headX = (name) => +ICONS[name].match(/class="ic-shaft" d="M([\d.]+) /)[1];
  assert.ok(headX('undo') < 12, "undo's head is on the LEFT");
  assert.ok(headX('redo') > 12, "redo's head is on the RIGHT");
  for (const name of ['undo', 'redo']) {
    const [shaft, head] = MOTION.icons[name].parts;
    assert.deepEqual([shaft.hook, head.hook], ['ic-shaft', 'ic-head']);
    assert.ok(shaft.keyframes[0].dashOffset < 0,
      `${name}: the shaft is drawn from its far END — tail to head, not head to tail`);
    // The head is struck as the shaft lands (the `x` cross's overlap), and finishes after it.
    assert.ok(head.delayMs > shaft.durationMs * 0.8
              && head.delayMs + head.durationMs > shaft.durationMs,
      `${name}: the head is struck last`);
  }
  // The quarter-turn buttons turn a WHOLE revolution, each the way it turns the image.
  const spin = (name) => MOTION.icons[name].parts[0].keyframes.at(-1).rotate;
  assert.equal(spin('rotate-ccw'), -360);
  assert.equal(spin('rotate-cw'), 360);
  assert.equal(spin('refresh-cw'), 360, 'the live-sync wheel completes its turn too');
  assert.ok(to('chevron-up', null).translate[1] < 0 && to('chevron-down', null).translate[1] > 0);
  assert.ok(to('chevron-left', null).translate[0] < 0 && to('chevron-right', null).translate[0] > 0);
  // The two halves of the chain move TOWARDS each other, not apart.
  const [a, b] = ['ic-link-a', 'ic-link-b'].map((h) => to('link', h).translate);
  assert.ok(a[0] < 0 && a[1] > 0 && b[0] > 0 && b[1] < 0, 'the links join');
  // The disguise comes APART: the hat lifts off, the glasses drop away from it.
  assert.ok(to('incognito', 'ic-brim').translate[1] < 0, 'the hat goes up');
  assert.ok(to('incognito', 'ic-glasses').translate[1] > 0, 'the glasses go down');
  assert.match(ICONS.incognito, /<g class="ic-glasses">(<circle[^>]*\/>){2}<path[^>]*\/><\/g>/,
    'both lenses and the bridge move as one');
});

test('plus draws itself — the vertical stroke, then the horizontal; minus shrinks', () => {
  // Both crosses are drawn the way you'd write one: markup order IS the sequence
  // (the desktop staggers on it), so the vertical stroke must come first.
  for (const [name, len] of [['plus', 14], ['plus-circle', 8]]) {
    const part = MOTION.icons[name].parts[0];
    assert.equal(part.hook, 'ic-stroke', `${name}: the strokes carry the hook`);
    assert.equal((ICONS[name].match(/class="ic-stroke"/g) || []).length, 2);
    assert.match(ICONS[name], /<line class="ic-stroke" x1="(\d+)" y1="\d+" x2="\1" /,
      `${name}: the FIRST hooked stroke is the vertical one (x1 === x2)`);
    assert.equal(part.dashArray, len, `${name}: one dash covers a whole ${len}-unit stroke`);
    assert.equal(part.keyframes[0].dashOffset, len, `${name}: it starts undrawn`);
    assert.equal(part.keyframes.at(-1).dashOffset, 0, `${name}: …and ends whole`);
    assert.equal(part.stagger, part.durationMs,
      `${name}: the horizontal starts exactly as the vertical lands`);
  }
  const peakM = MOTION.icons.minus.parts[0].keyframes.find((k) => k.at > 0 && k.at < 100).scale;
  assert.ok(peakM < 1, 'a minus that swelled would read as "increase"');
  assert.equal(MOTION.icons.minus.parts[0].keyframes.at(-1).scale, 1,
    'minus settles back to its default size');
  assert.match(SECTION, /\.ic-plus \.ic-stroke\s+\{[^}]*--ic-play: icmDrawPlus;/);
  assert.match(SECTION, /\.ic-plus \.ic-stroke:nth-of-type\(2\) \{ --ic-delay: 0\.2s; \}/);
  assert.match(SECTION, /\.ic-plus-circle \.ic-stroke:nth-of-type\(2\) \{ --ic-delay: 0\.2s; \}/);
  assert.match(SECTION, /\.ic-minus\s+\{[^}]*--ic-play: icmShrink;/);
});

test('the fullscreen corners extend to enter and retract to leave', () => {
  const d = MOTION.icons.maximize;
  const out = Object.fromEntries(d.parts.map((p) => [p.hook, p.to.translate]));
  const back = Object.fromEntries(d.variants.active.parts.map((p) => [p.hook, p.to.translate]));
  // Base: every corner moves away from the centre. Active (already fullscreen): the
  // exact opposite, so the motion always shows where the click takes you.
  assert.deepEqual(out['ic-corner-tl'], [-1.4, -1.4]);
  assert.deepEqual(out['ic-corner-br'], [1.4, 1.4]);
  for (const hook of Object.keys(out))
    assert.deepEqual(back[hook], out[hook].map((v) => -v), `${hook} inverts under .active`);
  assert.equal(d.stateHook, 'active');
  assert.match(SECTION, /\.active \.ic-maximize \.ic-corner-tl/, 'and the sheet keys on it');
});

test('the sun turns by ONE RAY and the moon waves — each as one whole glyph', () => {
  for (const name of ['moon', 'sun']) {
    const parts = MOTION.icons[name].parts;
    assert.equal(parts.length, 1, `${name}: the whole glyph moves, not choreographed parts`);
    assert.equal(parts[0].hook, null, `${name}: hook null = the whole glyph`);
    // Both are pure rotations about the glyph centre, so neither can leave its own box.
    for (const pose of parts[0].keyframes || [parts[0].to])
      for (const prop of ['translate', 'scale', 'scaleX', 'scaleY', 'skewX'])
        assert.equal(pose[prop], undefined, `${name}: the theme pair turn, and do nothing else`);
  }
  // Eight rays 45° apart, so one ray-space is the smallest turn that still leaves the glyph looking
  // untouched — the `gear`'s one-tooth idea; a whole revolution reads as a spin.
  const rays = (ICONS.sun.match(/<line /g) || []).length;
  assert.equal(rays, 8, 'the sun is drawn with eight rays');
  assert.equal(MOTION.icons.sun.mode, 'hold', 'held, so it eases back the same way it went');
  assert.equal(MOTION.icons.sun.parts[0].to.rotate, 360 / rays, 'exactly one ray of turn');
  // The moon WAVES: a damped rock, each swing reversing the last and settling upright.
  // Small on purpose — a crescent tipped far enough reads as a different shape.
  const rocks = MOTION.icons.moon.parts[0].keyframes.map((k) => k.rotate);
  assert.equal(rocks.at(0), 0);
  assert.equal(rocks.at(-1), 0, 'the moon comes back upright');
  const swings = rocks.slice(1, -1);
  assert.ok(swings.length >= 3, 'a wave needs more than one swing');
  for (let i = 1; i < swings.length; i++)
    assert.ok(swings[i] * swings[i - 1] < 0, 'each swing reverses the last');
  for (const r of swings) assert.ok(Math.abs(r) <= 12, `${r}° is a tumble, not a wave`);
  assert.ok(Math.abs(swings.at(-1)) < Math.abs(swings[0]), 'and the wave damps out');
  assert.match(SECTION, /\.ic-sun\s+\{[^}]*rotate\(calc\(var\(--ic-on\) \* 45deg\)\);/);
  assert.match(SECTION, /\.ic-moon\s+\{[^}]*--ic-play: icmRock;/);
  assert.doesNotMatch(SECTION, /animation-iteration-count/, 'a settle plays once');
});

test('the eraser erases a line, and writes it back', () => {
  const [body, base] = MOTION.icons.eraser.parts;
  assert.deepEqual([body.hook, base.hook], ['ic-body', 'ic-baseline']);
  // The pad sweeps along the line under it — right, the way the baseline runs from the
  // pad's own corner — and the line disappears from behind it by exactly that much.
  const sweep = body.keyframes.find((k) => k.translate).translate;
  assert.ok(sweep[0] > 0 && sweep[1] === 0, 'the pad rubs ALONG the line, not across it');
  const eaten = base.keyframes.map((k) => k.dashOffset);
  assert.deepEqual([eaten.at(0), eaten.at(-1)], [0, 0], 'the line is whole at both ends');
  assert.equal(Math.max(...eaten), sweep[0], 'and vanishes exactly as far as the pad travels');
  // A positive offset on a 15-unit baseline drawn (22,21)→(7,21) eats it from x=7 — the
  // end the pad starts on. Both live in the SAME element so the pad can cover the join.
  assert.equal(base.dashArray, 15);
  assert.match(ICONS.eraser, /<line class="ic-baseline" x1="22" y1="21" x2="7" y2="21"\/>/);
  assert.match(SECTION, /\.ic-eraser \.ic-body\s+\{[^}]*--ic-play: icmRub;/);
  assert.match(SECTION, /\.ic-eraser \.ic-baseline\s+\{[^}]*--ic-play: icmRubOut;/);
});

test('the help mark trembles inside its ring, and never turns out of it', () => {
  const part = MOTION.icons.help.parts[0];
  assert.equal(part.hook, 'ic-mark', 'the ring is not part of the motion, so it cannot move');
  for (const k of part.keyframes) {
    // Rotating the mark about the icon centre [12,12] swung the ? out past the ring —
    // the bug this design replaced. Translation keeps it in.
    assert.equal(k.rotate, undefined, 'a tremor is a translation, not a rotation');
    if (!k.translate) continue;
    assert.equal(k.translate[1], 0, 'side to side only');
    assert.ok(Math.abs(k.translate[0]) <= 1, 'and small: the mark stays inside the ring');
  }
  // The mark's ink spans x ≈ 9.1–14.9 inside a ring of r=10 about [12,12], so a ±1 shift
  // still leaves ~4 units of clearance on either side.
  assert.ok(ICONS.help.startsWith('<circle cx="12" cy="12" r="10"/>'));
  assert.match(SECTION, /\.ic-help \.ic-mark\s+\{[^}]*--ic-play: icmTremor;/);
});

test('the close cross is struck out one stroke at a time', () => {
  const part = MOTION.icons.x.parts[0];
  assert.equal(part.hook, 'ic-stroke');
  assert.equal((ICONS.x.match(/class="ic-stroke"/g) || []).length, 2, 'both strokes are hooked');
  // Each stroke runs (18,6)→(6,18): 12√2 ≈ 16.97 units, so one dash covers it whole.
  assert.ok(part.dashArray >= 17 && part.dashArray < 18, 'the dash spans a whole stroke');
  assert.equal(part.keyframes[0].dashOffset, part.dashArray, 'it starts undrawn');
  assert.equal(part.keyframes.at(-1).dashOffset, 0, '…and ends whole');
  // The second stroke starts exactly as the first lands. The cross runs 1.5× the base
  // draw-on speed of the other settle designs — deliberately slower, not a mistake.
  assert.equal(part.stagger, part.durationMs, 'the second starts as the first finishes');
  const total = part.durationMs + part.stagger;
  assert.ok(total >= 450 && total <= 570, `${total}ms is out of the cross's slowed range`);
  assert.match(SECTION, /\.ic-x \.ic-stroke:nth-of-type\(2\) \{ --ic-delay: 0\.27s; \}/);
});
