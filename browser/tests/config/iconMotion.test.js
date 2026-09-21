// config/iconMotion.json is the CANONICAL per-icon hover design table and config/icons.json carries
// the hooks it addresses; the browser implements it in css/animations.css, while the desktop (qrc →
// support/iconSet.cpp) and the extension render the same glyphs. Pinned across the three sides:
// every icon has a design and every design a real icon and hook, the hooks stay INERT (attributes
// and plain <g> wrappers only), and the CSS implements every design without moving a box.
import { test } from 'node:test';
import assert from 'node:assert/strict';

import ICONS from '../../js/config/icons.json' with { type: 'json' };
import MOTION from '../../js/config/iconMotion.json' with { type: 'json' };
import { ANIMATIONS_CSS } from '../helpers/css.js';

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
  // --ic-play is registered NON-inheriting: inherited, the same keyframes ran a second time on every
  // descendant and composed into DOUBLE the canonical pose, so the json numbers stopped meaning it.
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

// These designs carry an explicit meaning, so the motion has to agree with the word — which is what
