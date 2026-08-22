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
import { readFileSync } from 'node:fs';

import ICONS from '../js/config/icons.json' with { type: 'json' };
import MOTION from '../js/config/iconMotion.json' with { type: 'json' };

const css = readFileSync(new URL('../css/animations.css', import.meta.url), 'utf8');
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
// The rest pose: no move, no turn, full size, the stroke fully drawn.
const REST = { translate: [0, 0], rotate: 0, rotateX: 0, scale: 1, scaleX: 1, scaleY: 1, skewX: 0, dashOffset: 0 };
const isIdentity = (pose) => Object.entries(pose)
  .every(([k, v]) => k === 'at' || JSON.stringify(v) === JSON.stringify(REST[k]));

test('every canonical icon has a motion design, and every design an icon', () => {
  assert.deepEqual(Object.keys(MOTION.icons), Object.keys(ICONS),
    'iconMotion.json must cover icons.json exactly, in the same order');
  assert.equal(Object.keys(ICONS).length, 65);
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

test('nothing in the icon-motion section can move a box', () => {
  const SAFE = /^(--[\w-]+|transform|transform-origin|transform-box|transition|overflow|animation-name|animation-duration|animation-delay|animation-timing-function|animation-fill-mode|stroke-dasharray|stroke-dashoffset|background|border-color)$/;
  const body = SECTION.replace(/\/\*[\s\S]*?\*\//g, '');
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
  assert.ok(to('undo', null).translate[0] < 0 && to('redo', null).translate[0] > 0);
  assert.equal(to('rotate-ccw', null).rotate, -to('rotate-cw', null).rotate);
  assert.ok(to('chevron-up', null).translate[1] < 0 && to('chevron-down', null).translate[1] > 0);
  assert.ok(to('chevron-left', null).translate[0] < 0 && to('chevron-right', null).translate[0] > 0);
  // The two halves of the chain move TOWARDS each other, not apart.
  const [a, b] = ['ic-link-a', 'ic-link-b'].map((h) => to('link', h).translate);
  assert.ok(a[0] < 0 && a[1] > 0 && b[0] > 0 && b[1] < 0, 'the links join');
});

test('plus grows and minus shrinks — each settling back to the default size', () => {
  const peak = (name, hook) => MOTION.icons[name].parts
    .find((p) => p.hook === hook).keyframes.find((k) => k.at > 0 && k.at < 100).scale;
  assert.ok(peak('plus', null) > 1, 'plus GROWS');
  assert.ok(peak('plus-circle', 'ic-cross') > 1);
  assert.ok(peak('minus', null) < 1, 'a minus that swelled would read as "increase"');
  for (const [name, hook] of [['plus', null], ['minus', null], ['plus-circle', 'ic-cross']])
    assert.equal(MOTION.icons[name].parts.find((p) => p.hook === hook).keyframes.at(-1).scale, 1,
      `${name} settles back to its default size`);
  assert.match(SECTION, /\.ic-plus\s+\{[^}]*--ic-play: icmGrow;/);
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

test('sun and moon both RISE into place', () => {
  for (const [name, hook] of [['moon', null], ['sun', 'ic-orb']]) {
    const kf = MOTION.icons[name].parts.find((p) => p.hook === hook).keyframes;
    assert.ok(kf[0].translate[1] > 0, `${name} starts below its resting place`);
    assert.equal(kf.at(-1).translate, undefined, `${name} comes to rest in the glyph`);
  }
  // …and the sun's rays open out behind it, a beat later.
  const rays = MOTION.icons.sun.parts.find((p) => p.hook === 'ic-rays');
  assert.ok(rays.keyframes[0].scale < 1 && rays.delayMs > 0);
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
  assert.ok(lid.to.rotate > 0, 'and it lifts');
  assert.match(SECTION, /\.ic-trash \.ic-lid \{[^}]*transform-origin: 5px 6px;/);
  // The lid leaving the 24-unit box for a moment is why the glyphs stop clipping.
  assert.match(SECTION, /overflow: visible;/);
  assert.equal(MOTION.icons.folder.parts[0].to.rotateX < 0, true, 'the folder opens towards you');
  assert.ok(MOTION.icons.folder.parts[0].qtFallback, 'with a flat fallback for Qt');
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
