// Per-icon hover motion: the extension's half of the contract. browser/js/config/iconMotion.json is
// the CANONICAL table; the extension ships a subset of the glyphs on exactly the app's numbers.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { ICONS } from '../../src/lib/icons.js';
import {
  APP, APP_SEL, DESIGNED, EXT, EXTENSION_ONLY, MOTION,
  appRules, extRules, hooksOf, keyframesOf, partsOf, squash,
} from '../helpers/iconMotionCss.js';

// ── Coverage: the glyphs the extension carries, and only those ──────────────
test('every glyph the extension carries with a canonical design has motion', () => {
  const missing = [];
  for (const name of DESIGNED) {
    const d = MOTION.icons[name];
    assert.ok(d, `${name} is not extension-only, so it must have a canonical design`);
    if (d.mode === 'none') continue;
    if (!new RegExp(`\\.ic-${name}\\b`).test(EXT)) missing.push(name);
  }
  assert.deepEqual(missing, [], 'glyphs the extension shows but never animates');
  // The subset is a fact worth pinning: a glyph added to lib/icons.js needs motion too.
  assert.equal(DESIGNED.length, 31);
  assert.equal(Object.keys(ICONS).length, DESIGNED.length + EXTENSION_ONLY.length);
});

test('extension-only glyphs are declared, and stay still', () => {
  for (const name of EXTENSION_ONLY) {
    assert.equal(name in MOTION.icons, false,
      `"${name}" now exists canonically — take its design instead of leaving it unanimated`);
    assert.ok(!new RegExp(`\\.ic-${name}\\b`).test(EXT),
      `${name} has no canonical design, so the sheet must not invent one`);
  }
});

test('every hook a design addresses is in the glyph AND styled', () => {
  for (const name of DESIGNED) {
    const d = MOTION.icons[name];
    if (d.mode === 'none') continue;
    for (const hook of hooksOf(d)) {
      const uses = ICONS[name].split(`class="${hook}"`).length - 1;
      assert.ok(uses > 0,
        `${name}: the design moves .${hook}, but the extension's icons.js copy has no such part`);
      if (partsOf(d).some((p) => p.hook === hook && p.stagger))
        assert.ok(uses > 1, `${name}: .${hook} is staggered but marks one element`);
      assert.ok(EXT.includes(`.ic-${name} .${hook}`), `${name}: no rule for its .${hook} part`);
    }
  }
});

// ── Values: the same numbers as the app, not merely the same idea ───────────
test('every .ic- rule is byte-identical to the browser app\'s', () => {
  let compared = 0;
  for (const r of extRules) {
    if (r.prelude.startsWith('@') || !r.prelude.includes('.ic-')) continue;
    for (const sel of r.prelude.split(',').map(squash)) {
      const app = APP_SEL.get(sel);
      assert.ok(app, `"${sel}" exists here but not in browser/css/animations/iconHover.css`);
      assert.ok(app.includes(r.body),
        `"${sel}" drifted from the app:\n  ext: ${r.body}\n  app: ${app.join(' | ')}`);
      compared++;
    }
  }
  assert.ok(compared >= 40, `only ${compared} rules compared — the section moved?`);
});

test('the shared vocabulary — latch, timings and the 24-unit space — matches too', () => {
  for (const decl of [
    '--ic-on: 0;',
    'overflow: visible;',
    'transform-box: view-box;',
    'transition: transform var(--ic-ms, 0.2s) var(--ic-ease, cubic-bezier(0.16, 1, 0.3, 1));',
    'animation-duration: var(--ic-ms, 0.32s);',
    'animation-timing-function: var(--ic-ease, cubic-bezier(0.34, 1.2, 0.64, 1));',
    'animation-delay: var(--ic-delay, 0s);',
    'animation-fill-mode: both;',
    '--ic-on: 1;',
    'animation-name: var(--ic-play, none);',
    // An INHERITED --ic-play would run the same keyframes a second time on the children of whatever
    // named it, so both sheets register it non-inheriting; the canonical numbers hold only while they do.
    '@property --ic-play { syntax: "*"; inherits: false; }',
  ]) {
    assert.ok(EXT.includes(decl), `the extension sheet is missing "${decl}"`);
    assert.ok(APP.includes(decl), `"${decl}" is not the app's spelling any more`);
  }
});

test('the keyframes are the app\'s, and every one is played exactly as designed', () => {
  const mine = keyframesOf(extRules);
  const theirs = keyframesOf(appRules);
  const played = new Set([...EXT.matchAll(/--ic-play: (icm\w+);/g)].map((m) => m[1]));
  assert.deepEqual([...mine.keys()].filter((k) => !played.has(k)), [], 'unused keyframes');
  assert.deepEqual([...played].filter((k) => !mine.has(k)), [], 'keyframes named but not defined');
  for (const [name, body] of mine) {
    assert.ok(theirs.has(name), `@keyframes ${name} has no browser original`);
    assert.equal(body, theirs.get(name), `@keyframes ${name} drifted from the app`);
  }
  // A settle design must name a keyframe set; a hold must ride the latch instead.
  for (const name of DESIGNED) {
    const d = MOTION.icons[name];
    if (d.mode !== 'hold') continue;
    const rules = extRules.filter((r) => new RegExp(`\\.ic-${name}\\b`).test(r.prelude)
      && /transform:/.test(r.body));
    assert.ok(rules.length, `${name}: a hold design with no transform rule`);
    for (const r of rules)
      assert.match(r.body, /var\(--ic-on\)/, `${name}: a hold transform that ignores the latch`);
  }
});

