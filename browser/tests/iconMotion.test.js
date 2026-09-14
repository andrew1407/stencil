// ── Per-icon hover motion: the data contract ────────────────────────────────
// config/iconMotion.json is the CANONICAL design table — one motion per glyph, matched
// to what that glyph's action does — and config/icons.json carries the hooks it
// addresses (class="ic-lid", "ic-arrow", …). The browser implements it in
// css/animations.css; the desktop (qrc → support/iconSet.cpp) and the extension (its
// checked-in icons.js copy) render the same glyphs and are meant to mime the same
// things, so this file pins the three sides together:
//   • every canonical icon has a design, and every design names a real icon;
//   • every hook the design addresses exists in the glyph, and no hook is orphaned;
//   • the hooks stay INERT — attributes and plain <g> wrappers only, nothing a
//     non-CSS consumer (Qt's QSvgRenderer, the extension's copy) has to understand;
//   • the CSS implements every design, and nothing in it can move a box.
import { test } from 'node:test';
import assert from 'node:assert/strict';

import ICONS from '../js/config/icons.json' with { type: 'json' };
import MOTION from '../js/config/iconMotion.json' with { type: 'json' };
import { ANIMATIONS_CSS } from './helpers/css.js';

const css = ANIMATIONS_CSS;
// The icon-motion section of the sheet, comment header to the next section.
const SECTION = (() => {
  const from = css.indexOf('/* ── Icon hover: every glyph mimes its own action');
  const to = css.indexOf('/* ── App logo hover');
  assert.ok(from >= 0 && to > from, 'the icon-motion section is where the tests expect it');
  return css.slice(from, to);
})();

const partsOf = (design) => [
  ...design.parts,
  ...Object.values(design.variants || {}).flatMap((v) => v.parts),
];
const classesIn = (markup) => (markup.match(/class="([^"]+)"/g) || [])
  .map((m) => m.slice(7, -1));
// The rest pose: no move, full size, the stroke fully drawn — and, for a turn, a WHOLE
// number of revolutions, which leaves the glyph looking exactly as it started.
const REST = { translate: [0, 0], rotate: 0, rotateX: 0, scale: 1, scaleX: 1, scaleY: 1, skewX: 0, dashOffset: 0 };
const isIdentity = (pose) => Object.entries(pose).every(([k, v]) =>
  k === 'at' || (k === 'rotate' ? v % 360 === 0 : JSON.stringify(v) === JSON.stringify(REST[k])));

test('every canonical icon has a motion design, and every design an icon', () => {
  assert.deepEqual(Object.keys(MOTION.icons), Object.keys(ICONS),
    'iconMotion.json must cover icons.json exactly, in the same order');
  assert.equal(Object.keys(ICONS).length, 73);   // + the layout file pair, + minimize, + script
  // Non-canonical glyphs (the draw-mode pair lives inline in core/drawingApp.js) are
  // designed too, but kept OUT of `icons` so the 1:1 check above stays honest.
  assert.ok(MOTION.extras['draw-mode-icon'], 'the draw-mode faces are designed as an extra');
});

test('every design is well formed and says what the action is', () => {
  for (const [name, d] of Object.entries(MOTION.icons)) {
    assert.ok(d.action, `${name}: no action described`);
    assert.ok(['hold', 'settle', 'none'].includes(d.mode), `${name}: bad mode ${d.mode}`);
    for (const part of partsOf(d)) {
      assert.ok('hook' in part, `${name}: a part with no hook field (null = whole glyph)`);
      if (d.mode === 'hold') {
        assert.ok(part.to && Object.keys(part.to).length, `${name}: a hold part with no pose`);
        assert.ok(!part.keyframes, `${name}: hold parts do not carry keyframes`);
      }
      if (d.mode === 'settle') {
        const kf = part.keyframes;
        assert.ok(Array.isArray(kf) && kf.length >= 2, `${name}: settle needs keyframes`);
        assert.equal(kf[0].at, 0, `${name}: keyframes start at 0`);
        assert.equal(kf.at(-1).at, 100, `${name}: keyframes end at 100`);
        for (let i = 1; i < kf.length; i++)
          assert.ok(kf[i].at > kf[i - 1].at, `${name}: keyframe stops must ascend`);
        // A settle motion must RETURN to where it started — that is the whole point of
        // the mode (plus grows and comes back; minus shrinks and comes back).
        assert.ok(isIdentity(kf.at(-1)), `${name}: a settle must rest at the identity pose`);
      }
    }
  }
});

test('every hook the design addresses exists in the glyph, and none is orphaned', () => {
  const orphans = [];
  for (const [name, markup] of Object.entries(ICONS)) {
    const design = MOTION.icons[name];
    const hooks = new Set(partsOf(design).map((p) => p.hook).filter(Boolean));
    const present = new Set(classesIn(markup));
    for (const hook of hooks) {
      assert.ok(present.has(hook), `${name}: iconMotion addresses .${hook}, icons.json has no such part`);
      const uses = markup.split(`class="${hook}"`).length - 1;
      const staggered = partsOf(design).some((p) => p.hook === hook && p.stagger);
      if (staggered) assert.ok(uses > 1, `${name}: .${hook} is staggered but marks one element`);
    }
    for (const cls of present) if (!hooks.has(cls)) orphans.push(`${name}.${cls}`);
  }
  assert.deepEqual(orphans, [], 'hooks in icons.json that no design uses — drop them or design them');
});

test('the hooks are inert for every non-CSS consumer', () => {
  for (const [name, markup] of Object.entries(ICONS)) {
    // Drawing primitives + plain <g> grouping only: nothing here needs a stylesheet,
    // a script, or SMIL to render — Qt's QSvgRenderer draws the same picture.
    for (const [, tag] of markup.matchAll(/<(\w+)/g))
      assert.ok(['path', 'line', 'polyline', 'polygon', 'circle', 'rect', 'g'].includes(tag),
        `${name}: unexpected <${tag}> in a canonical glyph`);
    for (const cls of classesIn(markup))
      assert.match(cls, /^ic-[a-z0-9-]+$/, `${name}: hook "${cls}" is not an ic- hook`);
    assert.ok(!/\b(style|onload|class)="[^"]*[{;]/.test(markup), `${name}: no inline styling`);
    assert.ok(!/<(animate|script|style|set)\b/.test(markup), `${name}: no in-glyph animation`);
    // The wrappers we added carry a hook and nothing else; the one <g transform> that
    // predates them (message/sparkle) is left exactly as it was.
    for (const [, attrs] of markup.matchAll(/<g([^>]*)>/g))
      assert.ok(/^ class="ic-[a-z-]+"$/.test(attrs) || attrs === ' transform="translate(0 1.5)"',
        `${name}: a <g> wrapper doing more than grouping: ${attrs}`);
  }
});

test('the stylesheet implements every design', () => {
  const keyframes = new Set([...SECTION.matchAll(/@keyframes (icm\w+)/g)].map((m) => m[1]));
  const played = new Set([...SECTION.matchAll(/--ic-play: (icm\w+);/g)].map((m) => m[1]));
  assert.deepEqual([...keyframes].filter((k) => !played.has(k)), [], 'unused keyframes');
  assert.deepEqual([...played].filter((k) => !keyframes.has(k)), [], 'keyframes named but not defined');

  for (const [name, d] of Object.entries(MOTION.icons)) {
    if (d.mode === 'none') continue;
    assert.ok(SECTION.includes(`.ic-${name} `) || SECTION.includes(`.ic-${name},`) ||
              SECTION.includes(`.ic-${name}\n`) || SECTION.includes(`.ic-${name}{`),
      `${name}: designed ${d.mode}, but the sheet has no .ic-${name} rule`);
    for (const part of partsOf(d))
      if (part.hook)
        assert.ok(SECTION.includes(`.ic-${name} .${part.hook}`),
          `${name}: no rule for its .${part.hook} part`);
    // A hold pose is driven by the latch, so its declaration must multiply by --ic-on;
    // otherwise it would be a permanent transform, not hover feedback.
    if (d.mode === 'hold') {
      const rules = [...SECTION.matchAll(new RegExp(`\\.ic-${name}\\b[^{}]*\\{([^}]*)\\}`, 'g'))]
        .map((m) => m[1]).filter((body) => /transform:/.test(body));
      assert.ok(rules.length, `${name}: a hold design with no transform rule`);
      for (const body of rules)
        assert.match(body, /var\(--ic-on\)/, `${name}: a hold transform that ignores the latch`);
    }
  }
});

test('a design plays ONCE, on the element that names it — never again on its children', () => {
  // The trigger has to switch the animation on for the glyph AND every element inside it,
  // because a part hook can be any of them. An INHERITED --ic-play therefore ran the same
  // keyframes a second time on every descendant of the element that named it, and the two
  // composed: a <g> part or a whole-glyph design landed on DOUBLE its canonical pose (the
  // help mark turned 18° instead of 9° and swung out of its ring; the sun's rays bloomed
  // from scale 0.2 / -40°×2). Registering the property non-inheriting is the fix, and the
  // numbers in iconMotion.json only mean what they say while it holds.
  assert.match(SECTION, /@property --ic-play \{ syntax: "\*"; inherits: false; \}/);
  assert.match(SECTION, /animation-name: var\(--ic-play, none\);/);
  // Only --ic-play is registered: the latch and the timings are meant to inherit.
  assert.deepEqual([...SECTION.matchAll(/@property (--[\w-]+)/g)].map((m) => m[1]), ['--ic-play']);
});

test('nothing in the icon-motion section can move a box', () => {
  const SAFE = /^(--[\w-]+|transform|transform-origin|transform-box|transition|overflow|animation-name|animation-duration|animation-delay|animation-timing-function|animation-fill-mode|stroke-dasharray|stroke-dashoffset|background|border-color)$/;
  // `@property` registers a custom property's type; its body declares no style at all.
  const body = SECTION.replace(/\/\*[\s\S]*?\*\//g, '').replace(/@property[^{]*\{[^}]*\}/g, '');
  for (const [, block] of body.matchAll(/\{([^{}]*)\}/g))
    for (const decl of block.split(';'))
      if (decl.trim()) {
        const prop = decl.split(':')[0].trim();
        assert.match(prop, SAFE, `"${prop}" is not layout-safe — an icon hover must not reflow`);
      }
});

// ── The designs that carry an explicit meaning ──────────────────────────────
// These are the ones a well-meaning refactor could quietly invert, which is exactly the
// bug the generic tilt-and-swell had: the motion has to agree with the word.
test('direction is the meaning: the pairs point opposite ways', () => {
  const to = (name, hook) => MOTION.icons[name].parts.find((p) => p.hook === hook).to;
  assert.ok(to('download', 'ic-arrow').translate[1] > 0, 'download goes DOWN');
  assert.ok(to('upload', 'ic-arrow').translate[1] < 0, 'upload goes UP');
  // Undo and redo DRAW themselves now, so their direction lives in the glyph: the head
  // path runs left for undo and right for redo, and each is struck after its own shaft.
  // The shaft starts AT the head, so where it starts is which way the arrow points.
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
  // The sun's ray spacing IS its turn: eight rays, 45° apart, so one ray-space is the
  // smallest move that still leaves the glyph looking untouched — the `gear`'s one-tooth
  // idea. A whole revolution had to race to fit a hover, and read as a spin.
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
  // …within the clearance the frame leaves. Both are stroked at sw=2 (ui/icons.js), so
  // the sun's visual top is cy-r-1 and the frame's outline reaches inward to y+1: the
  // gap between them is the ENTIRE travel budget, and every keyframe stays inside it.
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
  // SVG's y axis points down, so a NEGATIVE angle turns the rim counter-clockwise —
  // the far end (x=21) rises off the can while the hinged stub stays put. A positive
  // angle would swing that end down INTO the can.
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
