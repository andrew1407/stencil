// The CSS half of the surface contract: the surface waits behind its motes, the layer is one
// canvas, reduced motion neutralises every class, and the injected modal carries the grain inline.
import test from 'node:test';
import assert from 'node:assert';
import { readFileSync } from 'node:fs';
import { animationsCss, motionSrc, themeCss } from './helpers/sources.js';
import { FLIGHTS, alphaAt } from '../src/lib/dust/cloud.js';

const css = (rel) => readFileSync(new URL(rel, import.meta.url), 'utf8');
const ANIMS = animationsCss();
const MOTION = motionSrc();
const THEME = themeCss();
const OVERLAY = css('../src/lib/drop/overlay.js');

// ── The CSS contract ────────────────────────────────────────────────────────
test('a dusted surface waits behind its motes, and its old pop stays off for good', () => {
  assert.match(ANIMS, /\.dust-driven \{ animation: none !important; \}/);
  assert.match(ANIMS, /\.dust-driven\.surface-forming \{ animation: stSurfaceForm var\(--dust-ms/);
  assert.match(ANIMS, /\.dust-driven\.surface-leaving \{ animation: stSurfaceLeave var\(--dust-ms/);
  // Held back while the motes gather, up as they land — and visible well before the last
  // one, so nothing you can already click stays invisible.
  assert.match(ANIMS, /@keyframes stSurfaceForm\s+\{ 0%, 55% \{ opacity: 0; \} 100% \{ opacity: 1; \} \}/);
  assert.match(ANIMS, /@keyframes stSurfaceLeave \{ 0% \{ opacity: 1; \} 16%, 100% \{ opacity: 0; \} \}/);
});

test("a surface's motes are visible from the FIRST frame, unlike a row's", () => {
  // The flights are dustCloud's table now (one particle system, shared with the browser
  // byte for byte): a surface's motes start visible, a row's from nothing.
  assert.equal(alphaAt(FLIGHTS.surfaceGather.alpha, 0), 0.55);
  assert.equal(alphaAt(FLIGHTS.surfaceScatter.alpha, 0), 1);
  assert.equal(alphaAt(FLIGHTS.gather.alpha, 0), 0);
  // A surface flies on its own, shorter clock; a row keeps the default span.
  assert.match(MOTION, /const gatherMs = toward \|\| !gather \? span : Math\.round\(span \* TILE_GATHER_SHARE\);/);
  assert.match(MOTION, /dur: gather \? gatherMs : Math\.max\(MIN_TILE_MS, span - m\.delay\)/);
  // …and the layer is one canvas, not a node per grain.
  assert.match(ANIMS, /\.disintegrate-host > canvas \{ position: absolute; display: block; \}/);
  assert.ok(!/disintegrate-tile/.test(ANIMS) && !/@keyframes stTile/.test(ANIMS), 'no rule left per tile');
});

test('reduced motion neutralises the surface classes the preference may have flipped under', () => {
  const tail = ANIMS.slice(ANIMS.indexOf('.disintegrate-host.dust-forming {'));
  assert.match(tail, /@media \(prefers-reduced-motion: reduce\) \{[\s\S]*?\.dust-driven, \.dust-driven\.surface-forming, \.dust-driven\.surface-leaving \{ animation: none !important; \}/);
  assert.match(ANIMS, /\.disintegrate-host \{ display: none; \}/, 'no motes at all under the preference');
});

test('the tooltip keeps its mask grain as the fallback under the real motes', () => {
  // The three coprime dot grids ramped by --dissolve stay as the rest state and the reduced-motion /
  // declined-dust fallback; `.dust-driven` steps them aside once real motes have flown.
  assert.match(THEME, /#app-tooltip \{[\s\S]*?--dissolve: 1;/);
  assert.match(THEME, /#app-tooltip\.visible \{[\s\S]*?--dissolve: 0;/);
  assert.match(THEME, /mask-size: 4px 4px, 7px 7px, 11px 11px;/);
  assert.match(THEME, /mask-position: 0 0, 2px 3px, 5px 1px;/);
  assert.match(THEME, /mask-composite: add;/);
  // At 120% the dots overlap outright, so a SETTLED tooltip is solid to the pixel —
  // decoration must never cost legibility.
  assert.match(THEME, /#000 max\(0%, calc\(120% - var\(--dissolve\) \* 160%\)\)/);
  // --dissolve can only be transitioned because animations/reveal.css registers it.
  assert.match(ANIMS, /@property --dissolve \{ syntax: "<number>"; inherits: false; initial-value: 0; \}/);
  // …and once a real cloud has flown, the mask and the transition are off for good.
  assert.match(THEME, /#app-tooltip\.dust-driven \{[\s\S]*?mask-image: none;/);
  const tip = readFileSync(new URL('../src/lib/tip/controlTooltip.js', import.meta.url), 'utf8');
  assert.match(tip, /surfaceIn\(t, dustPoint\(el\), \{ ms: TIP_IN_MS \}\)/);
  assert.match(tip, /surfaceOut\(tip, dustPoint\(owner\), \{ ms: TIP_OUT_MS \}\)/);
});

test('the injected in-page modal carries the same grain inline (it can link nothing)', () => {
  // MV3: overlay.js is serialized into the HOST page, so its motion has to be self-contained.
  assert.match(OVERLAY, /const grain = \(d\) => \{/);
  assert.match(OVERLAY, /120 - rate \* d/);
  assert.match(OVERLAY, /stop\(160\)\},\$\{stop\(140\)\},\$\{stop\(125\)\}/);
  assert.match(OVERLAY, /mask-size:\$\{SIZES\}/, 'the cell sizes ride along in every step');
  assert.match(OVERLAY, /const SIZES = '4px 4px,7px 7px,11px 11px';/);
  assert.match(OVERLAY, /grainFrames\('stencilPanelIn'/);
  assert.match(OVERLAY, /grainFrames\('stencilPanelOut'/);
  // …and it disperses on the way out instead of blinking away — idempotently, so every
  // close route (Escape, backdrop, the ✕, the ready timeout) lands on "gone" exactly once.
  assert.match(OVERLAY, /if \(leaving\) return;\s*\n\s*leaving = true;/);
  assert.match(OVERLAY, /wrap\.classList\.add\('leaving'\);/);
  assert.match(OVERLAY, /host\.style\.pointerEvents = 'none';/);
  assert.match(OVERLAY, /setTimeout\(\(\) => host\.remove\(\), LEAVE_MS\);/);
  // A half-formed panel is a surprise, not motion.
  assert.match(OVERLAY, /\.panel\{-webkit-mask-image:none !important;mask-image:none !important;\}/);
});
