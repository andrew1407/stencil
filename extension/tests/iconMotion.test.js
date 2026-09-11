// ── Per-icon hover motion: the extension's half of the contract ──────────────
// browser/js/config/iconMotion.json is the CANONICAL design table — ONE motion per glyph,
// matched to what that glyph's action DOES. The browser implements it in
// css/animations/iconHover.css; the extension ships a deliberate SUBSET of the glyphs
// (lib/icons.js, pinned by dataParity.test.js) and implements the SAME designs on the SAME
// numbers in lib/animations.css, so the two match rather than merely both move.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';

import { ICONS } from '../src/lib/icons.js';

const read = (rel) => readFileSync(new URL(rel, import.meta.url), 'utf8');
const MOTION = JSON.parse(read('../../browser/js/config/iconMotion.json'));

// The icon-motion section of a sheet: its comment header up to the next section.
const section = (css, from, to) => {
  const a = css.indexOf(from);
  const b = css.indexOf(to);
  assert.ok(a >= 0 && b > a, `the icon-motion section is where the tests expect it (${from})`);
  return css.slice(a, b);
};
const EXT = section(read('../src/lib/animations.css'),
  '/* ── Icon hover: every glyph mimes its own action', '/* ── Header logo hover');
const APP = section(read('../../browser/css/animations/iconHover.css'),
  '/* ── Icon hover: every glyph mimes its own action', '/* ── App logo hover');

// Glyphs lib/icons.js carries that the browser has no twin for (dataParity.test.js
// holds the same list): they have no canonical design and must stay still.
const EXTENSION_ONLY = ['sidebar', 'type'];
const DESIGNED = Object.keys(ICONS).filter((n) => !EXTENSION_ONLY.includes(n));

// ── A tiny brace-aware CSS reader ───────────────────────────────────────────
// Enough for this section: plain rules, @keyframes (a leaf — its body is compared
// whole), and one @media block. Comments are stripped; whitespace is collapsed so
// two sheets that only differ in indentation still compare equal.
const squash = (s) => s.replace(/\s+/g, ' ').trim();
const parse = (css) => {
  const rules = [];        // { at, prelude, body }
  const walk = (text, at) => {
    let i = 0;
    while (i < text.length) {
      const open = text.indexOf('{', i);
      if (open < 0) break;
      const prelude = squash(text.slice(i, open));
      let depth = 1, j = open + 1;
      while (j < text.length && depth) {
        if (text[j] === '{') depth++;
        else if (text[j] === '}') depth--;
        j++;
      }
      const body = text.slice(open + 1, j - 1);
      if (/^@(media|supports)/.test(prelude)) walk(body, prelude);
      else rules.push({ at, prelude, body: squash(body) });
      i = j;
    }
  };
  walk(css.replace(/\/\*[\s\S]*?\*\//g, ''), '');
  return rules;
};
const extRules = parse(EXT);
const appRules = parse(APP);
// selector (one comma-separated part) → the bodies declared for it
const bySelector = (rules) => {
  const map = new Map();
  for (const r of rules) {
    if (r.prelude.startsWith('@')) continue;
    for (const sel of r.prelude.split(',').map(squash))
      map.set(sel, [...(map.get(sel) || []), r.body]);
  }
  return map;
};
const APP_SEL = bySelector(appRules);
const keyframesOf = (rules) => new Map(rules
  .filter((r) => r.prelude.startsWith('@keyframes'))
  .map((r) => [r.prelude.replace('@keyframes', '').trim(), r.body]));

const partsOf = (d) => [...d.parts, ...Object.values(d.variants || {}).flatMap((v) => v.parts)];
const hooksOf = (d) => [...new Set(partsOf(d).map((p) => p.hook).filter(Boolean))];

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
    // The trigger switches the animation on for the glyph AND every element inside it, so
    // an INHERITED --ic-play would run the same keyframes a second time on the children of
    // whatever named it, doubling every <g>-hooked and whole-glyph pose. Both sheets
    // register it non-inheriting; the canonical numbers only hold while they do.
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

// ── The contracts the design table writes down ──────────────────────────────
test('no layout shift: nothing in the section can move a box', () => {
  const SAFE = /^(--[\w-]+|transform|transform-origin|transform-box|transition|overflow|animation-name|animation-duration|animation-delay|animation-timing-function|animation-fill-mode|stroke-dasharray|stroke-dashoffset)$/;
  for (const r of extRules) {
    // @keyframes only pose the glyph (checked below); @property registers a custom
    // property's type and declares no style at all.
    if (r.prelude.startsWith('@')) continue;
    for (const decl of r.body.split(';')) {
      if (!decl.trim()) continue;
      const prop = decl.split(':')[0].trim();
      assert.match(prop, SAFE, `"${prop}" is not layout-safe — an icon hover must not reflow`);
    }
  }
  // …and the same holds inside the keyframes, which only ever pose the glyph.
  for (const body of keyframesOf(extRules).values())
    for (const [, prop] of body.matchAll(/([a-z-]+)\s*:/g))
      assert.ok(['transform', 'stroke-dashoffset'].includes(prop), `keyframe touches "${prop}"`);
});

test('reduced motion leaves every glyph in its rest pose', () => {
  assert.match(MOTION.trigger.reducedMotion, /prefers-reduced-motion/);
  const reduced = extRules.filter((r) => /prefers-reduced-motion/.test(r.at));
  const latch = reduced.find((r) => r.prelude === '.ic, .ic *');
  assert.ok(latch, 'the section carries its own reduced-motion block for .ic and its parts');
  for (const decl of ['--ic-on: 0 !important', 'animation-name: none !important',
                      'transition: none !important'])
    assert.ok(latch.body.includes(decl), `reduced motion must set ${decl}`);
  // The rest pose IS every motion's end state, so nothing may reset a transform outright
  // — that would undo a pose some other rule owns.
  for (const r of reduced)
    assert.ok(!/(^|;)\s*transform\s*:/.test(r.body), 'reduced motion must not reset a transform');
});

test('the fold chevrons opt out — their rotation is state, not hover feedback', () => {
  assert.ok(MOTION.trigger.excluded.some((e) => /toggle-/.test(e)),
    'the canonical table excludes the fold toggles');
  const guard = extRules.find((r) => r.prelude === '.chev, .chev *');
  assert.ok(guard, 'the sheet keeps the fold chevrons off the latch');
  assert.match(guard.body, /--ic-on: 0 !important/);
  assert.match(guard.body, /animation-name: none !important/);
  // …and in the markup they are `.chev`, never `.ic`, so they never enter the latch.
  for (const page of ['popup/popup', 'sidepanel/sidepanel', 'devtools/panel']) {
    const html = read(`../src/${page}.html`);
    assert.ok(html.includes('class="chev"'), `${page}.html has fold chevrons`);
    assert.ok(!/class="chev[^"]*\bic\b/.test(html), `${page}.html: a fold chevron became an .ic`);
  }
});

test('the hover trigger excludes the controls that already own their glyph', () => {
  const trigger = extRules.find((r) => /:hover/.test(r.prelude) && /--ic-on: 1/.test(r.body));
  assert.ok(trigger, 'a hover rule flips the latch');
  for (const not of [':not(.is-loading)', ':not(.swapping)'])
    assert.ok(trigger.prelude.includes(not), `the trigger should carry ${not}`);
  // A DISABLED control is not one of them (browser iconMotion.json trigger.disabled): it
  // is still hovered and still explains itself, and a frozen glyph read as a dead strip.
  assert.ok(!trigger.prelude.includes(':not(:disabled)'), 'a greyed control animates too');
  assert.match(trigger.prelude, /:is\(\.ic, \.ic \*\)/, 'the glyph and its parts both latch');
});

// ── The host pages and the injected shell ───────────────────────────────────
test('every inline glyph copy in a host page names itself, so it animates too', () => {
  let found = 0;
  for (const page of ['popup/popup', 'sidepanel/sidepanel', 'devtools/panel', 'crop/crop']) {
    const html = read(`../src/${page}.html`);
    for (const m of html.matchAll(/<svg class="ic([^"]*)"[^>]*>([\s\S]*?)<\/svg>/g)) {
      const cls = m[1].trim();
      assert.match(cls, /^ic-[a-z0-9-]+$/,
        `${page}.html: an inline .ic glyph with no ic-<name> class — it can never animate`);
      const name = cls.slice(3);
      assert.ok(name in ICONS, `${page}.html: "${name}" is not a glyph lib/icons.js knows`);
      assert.equal(m[2], ICONS[name], `${page}.html: the inline copy of "${name}" drifted`);
      found++;
    }
  }
  assert.equal(found, 9, 'the inline copies are the rescan / settings / sidebar / rotate pair');
});

test('the injected overlay shell mimes the same two actions', () => {
  const src = read('../src/lib/overlay.js');
  // It is injected into the host page and cannot link lib/animations.css, so it carries
  // the two designs inline — on the canonical numbers.
  assert.ok(src.includes('<g class="ic-arrow">'), 'the "open in a tab" arrow carries its hook');
  assert.deepEqual(MOTION.icons.external.parts[0].to.translate, [1.4, -1.4]);
  assert.match(src, /\.ic-arrow\{transform:translate\(calc\(var\(--ic-on\)\*1\.4px\),calc\(var\(--ic-on\)\*-1\.4px\)\)/,
    'and the arrow leaves the box by the canonical 1.4 units');
  // …and the close cross is struck out one stroke at a time, on the canonical dash and
  // the canonical stagger — the second stroke starting as the first lands.
  const strokes = MOTION.icons.x.parts[0];
  assert.equal(strokes.hook, 'ic-stroke');
  assert.equal((src.match(/class="ic-stroke"/g) || []).length, 2,
    'both of the shell cross\'s strokes carry the hook');
  assert.match(src, new RegExp(`stencilDrawSlash\\{from\\{stroke-dashoffset:${strokes.dashArray}\\}to\\{stroke-dashoffset:0\\}\\}`));
  assert.ok(src.includes(`.bar button.close svg .ic-stroke{stroke-dasharray:${strokes.dashArray};}`));
  const secs = (ms) => `${ms / 1000}`.replace(/^0/, '');
  assert.ok(src.includes(`animation:stencilDrawSlash ${secs(strokes.durationMs)}s`));
  assert.ok(src.includes(`.ic-stroke:nth-of-type(2){animation-delay:${secs(strokes.stagger)}s;}`));
  assert.match(src, /\.bar button svg,\.bar button svg \*\{--ic-on:0 !important;animation:none !important;transition:none !important;\}/,
    'and the shell honours prefers-reduced-motion');
});

// ── The designs, as this surface implements them ────────────────────────────
// The canonical table's own rules (direction, the sun's single ray, the moon's swings)
// are the browser suite's — these pin that the extension's sheet and glyph copies carry
// the designs the table describes.
test('the designs that read backwards if inverted are wired right here too', () => {
  const to = (name, hook) => MOTION.icons[name].parts.find((p) => p.hook === hook).to;
  // The trash lid is hinged at the left end of the rim, and it lifts.
  assert.deepEqual(MOTION.icons.trash.parts[0].origin, [5, 6]);
  assert.match(EXT, /\.ic-trash \.ic-lid \{[^}]*transform-origin: 5px 6px;/);
  // The disguise comes APART: the hat lifts off, the glasses drop away from it.
  assert.ok(to('incognito', 'ic-brim').translate[1] < 0);
  assert.ok(to('incognito', 'ic-glasses').translate[1] > 0);
  assert.match(ICONS.incognito, /<g class="ic-glasses">/, 'the extension copy carries the hook');
  // …which is why the glyphs stop clipping.
  assert.match(EXT, /overflow: visible;/);
});

test('the sun turns by one ray, the moon waves, the assistant types, the LEDs blink', () => {
  // The sun's ray spacing IS its turn: eight rays, 45° apart, so one ray-space leaves the
  // glyph looking untouched. The extension's copy has to be drawn with those eight.
  assert.equal((ICONS.sun.match(/<line /g) || []).length, 8);
  assert.equal(MOTION.icons.sun.parts[0].to.rotate, 360 / 8);
  assert.match(EXT, /\.ic-sun\s+\{[^}]*rotate\(calc\(var\(--ic-on\) \* 45deg\)\);/);
  assert.match(EXT, /\.ic-moon\s+\{[^}]*--ic-play: icmRock;/);
  // Three dots, one bounce each, left to right — the typing idiom.
  const dots = MOTION.icons.sparkle.parts[0];
  assert.equal(dots.hook, 'ic-dot');
  assert.equal((ICONS.sparkle.match(/class="ic-dot"/g) || []).length, 3);
  assert.match(EXT, /\.ic-sparkle \.ic-dot:nth-of-type\(3\)[^{]*\{[^}]*--ic-delay: 0\.14s;/);
  // The two status LEDs blink top unit then bottom, each about its own centre.
  assert.match(EXT, /\.ic-server \.ic-led:nth-of-type\(1\) \{ transform-origin: 6px 6px; \}/);
  assert.match(EXT, /\.ic-server \.ic-led:nth-of-type\(2\) \{ transform-origin: 6px 18px; --ic-delay: 0\.11s; \}/);
});
