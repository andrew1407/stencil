// Tests for src/lib/accent.js — the extension's accent + Appearance store.
//
// It is loaded as a classic <script> in every extension page's <head> BEFORE lib/theme.css,
// so the saved choice is stamped on <html> before first paint (no flash). That makes it
// untestable by import; tests/helpers/accentSandbox.js runs it in a fabricated page instead.
//
// The list it owns is duplicated on purpose in two places that CANNOT import it — the
// service worker / page-bridge side (lib/highlightColor.js ACCENT_HEX) and the browser app
// (canonical rows in browser/js/config/accents.json). The parity tests at the bottom — and
// tests/dataParity.test.js — enforce the sync.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';

import { loadAccent } from './helpers/accentSandbox.js';
import { ACCENT_HEX, ACCENT_STORAGE_KEY, DEFAULT_HL } from '../src/lib/highlightColor.js';
import { THEME_STORAGE_KEY, THEME_MODES } from '../src/lib/shellTheme.js';

// ── The accent list ──

test('publishes the twelve presets, each with a key, label and #rrggbb hex', () => {
  const { accent } = loadAccent();
  assert.equal(accent.list.length, 12);
  const keys = new Set();
  for (const a of accent.list) {
    assert.match(a.key, /^[a-z]+$/, `bad key ${a.key}`);
    assert.ok(a.label && typeof a.label === 'string', `missing label for ${a.key}`);
    assert.match(a.hex, /^#[0-9a-f]{6}$/i, `bad hex for ${a.key}`);
    assert.equal(keys.has(a.key), false, `duplicate key ${a.key}`);
    keys.add(a.key);
  }
});

test('the storage keys are the ones every other surface reads', () => {
  const { accent, theme } = loadAccent();
  assert.equal(accent.storageKey, 'stencil_accent');
  assert.equal(theme.storageKey, 'stencil_theme');
  // The mirrors in lib/highlightColor.js and lib/shellTheme.js must name the same keys.
  assert.equal(accent.storageKey, ACCENT_STORAGE_KEY);
  assert.equal(theme.storageKey, THEME_STORAGE_KEY);
});

test('hexOf resolves a preset, and falls back to the first entry for anything else', () => {
  const { accent } = loadAccent();
  assert.equal(accent.hexOf('sky'), '#0ea5e9');
  assert.equal(accent.hexOf('violet'), '#7c3aed');
  for (const bogus of ['nonsense', '', undefined, null, 'VIOLET']) {
    assert.equal(accent.hexOf(bogus), accent.list[0].hex, `hexOf(${bogus})`);
  }
});

// ── Reading and writing the accent ──

test('defaults to violet with nothing stored, and stamps it before first paint', () => {
  const page = loadAccent();
  assert.equal(page.accent.get(), 'violet');
  assert.equal(page.dataAccent(), 'violet', 'the attribute is set at load, not on first set()');
});

test('a stored accent is honoured at load', () => {
  const page = loadAccent({ stored: { stencil_accent: 'crimson' } });
  assert.equal(page.accent.get(), 'crimson');
  assert.equal(page.dataAccent(), 'crimson');
});

test('setCustom: a page-only inline --accent, not persisted; junk → null', () => {
  const page = loadAccent();
  assert.equal(page.accent.setCustom('#abc'), '#aabbcc', 'normalizes a 3-digit hex');
  assert.equal(page.inlineAccent(), '#aabbcc', 'the inline override is set');
  assert.equal(page.store.get('stencil_accent'), undefined, 'a custom accent never persists');
  assert.equal(page.accent.setCustom('nope'), null, 'invalid hex is rejected');
});

test('previewAccent paints a preset instantly (no persist); endAccentPreview reverts to the committed accent', () => {
  const page = loadAccent();
  page.accent.set('blue');                     // committed preset
  assert.equal(page.dataAccent(), 'blue');
  page.accent.previewAccent('pink');           // hover
  assert.equal(page.dataAccent(), 'pink', 'the page shows the previewed preset');
  assert.equal(page.store.get('stencil_accent'), 'blue', 'preview never persists');
  page.accent.endAccentPreview();
  assert.equal(page.dataAccent(), 'blue', 'leaving the list restores the committed preset');
});

test('previewAccent over a custom accent restores the inline hex on revert', () => {
  const page = loadAccent();
  page.accent.setCustom('#123456');
  page.accent.previewAccent('pink');
  assert.equal(page.dataAccent(), 'pink');
  assert.equal(page.inlineAccent(), '', 'the preview drops the inline override');
  page.accent.endAccentPreview();
  assert.equal(page.inlineAccent(), '#123456', 'the custom hex comes back');
});

test('a committed set during a preview supersedes it — endAccentPreview then no-ops', () => {
  const page = loadAccent();
  page.accent.set('blue');
  page.accent.previewAccent('pink');
  page.accent.set('aqua');                     // a real pick mid-hover
  page.accent.endAccentPreview();
  assert.equal(page.dataAccent(), 'aqua', 'the pick stands; no revert to blue');
});

// The real thing defers the swap's callback a beat, and the menu's own close ends the
// preview in that gap — so the supersede must already have happened by then, not inside
// the callback, or the pick is flooded away and the page keeps the old accent.
test('a pick supersedes the preview BEFORE its swap callback runs, not inside it', () => {
  const page = loadAccent({ deferViewTransitions: true });
  page.accent.set('blue');
  page.runSwaps();
  page.accent.previewAccent('pink');
  page.runSwaps();
  assert.equal(page.dataAccent(), 'pink');

  page.accent.set('aqua');                     // the pick — its paint is still queued
  page.accent.endAccentPreview();              // …and the menu closes on top of it
  page.runSwaps();
  assert.equal(page.dataAccent(), 'aqua', 'the pick stands; the close cannot flood it away');
});

test('a stored value that is not a preset falls back to the default', () => {
  const page = loadAccent({ stored: { stencil_accent: 'chartreuse' } });
  assert.equal(page.accent.get(), 'violet');
  assert.equal(page.dataAccent(), 'violet');
});

test('set() persists, stamps the attribute, and returns the applied key', () => {
  const page = loadAccent();
  assert.equal(page.accent.set('aqua'), 'aqua');
  assert.equal(page.store.get('stencil_accent'), 'aqua');
  assert.equal(page.dataAccent(), 'aqua');
  assert.equal(page.accent.get(), 'aqua');
});

test('set() sanitises an unknown key to the default rather than storing it', () => {
  const page = loadAccent();
  assert.equal(page.accent.set('not-a-colour'), 'violet');
  assert.equal(page.store.get('stencil_accent'), 'violet');
  assert.equal(page.dataAccent(), 'violet');
});

// ── The favicon ──

test('load creates a <link rel="icon"> carrying the accent hex', () => {
  const page = loadAccent({ stored: { stencil_accent: 'sky' } });
  const link = page.faviconLink();
  assert.ok(link, 'a favicon link should have been created');
  assert.equal(link.type, 'image/svg+xml');
  assert.ok(link.href.startsWith('data:image/svg+xml,'), link.href);
  // The accent is painted into the SVG (URL-encoded, so # becomes %23).
  assert.ok(
    decodeURIComponent(link.href).includes('#0ea5e9'),
    'the accent hex should appear in the favicon SVG',
  );
});

test('changing the accent updates the SAME link instead of appending another', () => {
  const page = loadAccent();
  const before = page.headChildren.length;
  page.accent.set('crimson');
  page.accent.set('grass');
  assert.equal(page.headChildren.length, before, 'no extra <link> elements');
  assert.ok(decodeURIComponent(page.faviconLink().href).includes('#16a34a'));
});

// ── Appearance (light / dark / system) ──

test('the three modes are the ones the shell mirrors', () => {
  const { theme } = loadAccent();
  // Spread across the realm boundary: the sandbox's Array has a different prototype,
  // which strict deepEqual counts as a difference.
  assert.deepEqual([...theme.modes], ['system', 'light', 'dark']);
  assert.deepEqual([...theme.modes], THEME_MODES);
});

test('defaults to system, and resolves against the OS', () => {
  const light = loadAccent({ prefersDark: false });
  assert.equal(light.theme.get(), 'system');
  assert.equal(light.theme.resolved(), 'light');
  assert.equal(light.dataTheme(), 'light');

  const dark = loadAccent({ prefersDark: true });
  assert.equal(dark.theme.get(), 'system');
  assert.equal(dark.theme.resolved(), 'dark');
  assert.equal(dark.dataTheme(), 'dark');
});

test('an explicit choice beats the OS in both directions', () => {
  const onDarkOs = loadAccent({ stored: { stencil_theme: 'light' }, prefersDark: true });
  assert.equal(onDarkOs.theme.get(), 'light');
  assert.equal(onDarkOs.dataTheme(), 'light', 'Light must stay light on a dark OS');

  const onLightOs = loadAccent({ stored: { stencil_theme: 'dark' }, prefersDark: false });
  assert.equal(onLightOs.dataTheme(), 'dark', 'Dark must stay dark on a light OS');
});

test('the CHOSEN mode is stored but the RESOLVED one is stamped', () => {
  const page = loadAccent({ prefersDark: true });
  page.theme.set('system');
  assert.equal(page.store.get('stencil_theme'), 'system', 'storage keeps the choice');
  assert.equal(page.dataTheme(), 'dark', 'the attribute carries the resolution');
});

test('set() sanitises an unknown mode to system', () => {
  const page = loadAccent({ prefersDark: true });
  assert.equal(page.theme.set('sepia'), 'system');
  assert.equal(page.store.get('stencil_theme'), 'system');
  assert.equal(page.dataTheme(), 'dark');
});

test('system keeps tracking the OS after load', () => {
  const page = loadAccent({ prefersDark: false });
  assert.equal(page.dataTheme(), 'light');
  page.setPrefersDark(true);
  assert.equal(page.dataTheme(), 'dark', 'the OS flipping must re-stamp the attribute');
});

test('an explicit choice ignores the OS flipping', () => {
  const page = loadAccent({ stored: { stencil_theme: 'light' }, prefersDark: false });
  page.setPrefersDark(true);
  assert.equal(page.dataTheme(), 'light', 'Light must survive the OS going dark');
});

// ── Mirroring into chrome.storage.local ──

test('both choices are mirrored for contexts that cannot read this localStorage', () => {
  const page = loadAccent({ stored: { stencil_accent: 'brown', stencil_theme: 'dark' } });
  const merged = Object.assign({}, ...page.mirrored);
  assert.equal(merged.stencil_accent, 'brown');
  assert.equal(merged.stencil_theme, 'dark');
});

test('a sanitised accent is mirrored as the sanitised value, not the raw one', () => {
  const page = loadAccent({ stored: { stencil_accent: 'bogus' } });
  const merged = Object.assign({}, ...page.mirrored);
  assert.equal(merged.stencil_accent, 'violet');
});

test('no chrome API (an ordinary page) is not fatal', () => {
  const page = loadAccent({ withChrome: false });
  assert.equal(page.accent.get(), 'violet');
  assert.equal(page.dataAccent(), 'violet');
  assert.equal(page.mirrored.length, 0);
});

// ── Private mode ──

test('a throwing localStorage degrades to the defaults instead of breaking the page', () => {
  const page = loadAccent({ storageThrows: true, prefersDark: true });
  assert.equal(page.accent.get(), 'violet');
  assert.equal(page.theme.get(), 'system');
  assert.equal(page.dataAccent(), 'violet');
  assert.equal(page.dataTheme(), 'dark', 'the OS preference still resolves');
});

test('writes are swallowed in private mode, and the attribute still updates', () => {
  const page = loadAccent({ storageThrows: true });
  assert.equal(page.accent.set('pink'), 'pink');
  assert.equal(page.dataAccent(), 'pink', 'the visual change applies for this page session');
  assert.equal(page.theme.set('dark'), 'dark');
  assert.equal(page.dataTheme(), 'dark');
});

test('no matchMedia is not fatal — it resolves light', () => {
  const page = loadAccent({ withMatchMedia: false });
  assert.equal(page.theme.resolved(), 'light');
  assert.equal(page.dataTheme(), 'light');
});

// ── Cross-page live sync ──

test('another page changing the accent re-applies here without a reload', () => {
  const page = loadAccent();
  assert.equal(page.dataAccent(), 'violet');
  // Another same-origin extension page wrote the key; the storage event fires here.
  page.store.set('stencil_accent', 'orange');
  page.fireStorage('stencil_accent');
  assert.equal(page.dataAccent(), 'orange');
});

test('another page changing the Appearance re-applies here', () => {
  const page = loadAccent({ prefersDark: false });
  page.store.set('stencil_theme', 'dark');
  page.fireStorage('stencil_theme');
  assert.equal(page.dataTheme(), 'dark');
});

test('localStorage.clear() elsewhere (key === null) resets both to their defaults', () => {
  const page = loadAccent({ stored: { stencil_accent: 'grey', stencil_theme: 'dark' } });
  assert.equal(page.dataAccent(), 'grey');
  page.store.clear();
  page.fireStorage(null);
  assert.equal(page.dataAccent(), 'violet');
  assert.equal(page.dataTheme(), 'light');
});

test('an unrelated key is ignored', () => {
  const page = loadAccent({ stored: { stencil_accent: 'aqua' } });
  page.store.set('stencil_accent', 'pink'); // changed underneath, but…
  page.fireStorage('someone_elses_key');    // …the event is for another key
  assert.equal(page.dataAccent(), 'aqua', 'only the accent/theme keys should trigger a re-apply');
});

test('onChange fires when another surface changes the Appearance', () => {
  const page = loadAccent();
  const seen = [];
  page.theme.onChange((mode) => seen.push(mode));
  page.store.set('stencil_theme', 'dark');
  page.fireStorage('stencil_theme');
  assert.deepEqual(seen, ['dark']);
});

// ── Where the palette-swap wipe starts ───────────────────────────────────────
// It has to come out of the CONTROL that changed the theme — the round toggle in the
// popup header — the way the desktop app blooms from its theme button. The trap was the
// pointer fallback: the last press is often an unrelated one, so it dragged the circle
// off into a window corner.
const TOGGLE = { id: 'theme-toggle', rect: { left: 276, top: 26, width: 28, height: 28 } };
const wipePage = (opts = {}) => loadAccent({ withViewTransitions: true, controls: [TOGGLE], ...opts });

test('the wipe starts at the theme button, and no press is remembered to override it', () => {
  const page = wipePage();
  assert.equal(page.pointerListeners.length, 0, 'the last click is never the origin');
  page.theme.set('dark');
  assert.deepEqual(page.swapOrigin(), { x: 290, y: 40 }, 'the centre of #theme-toggle');
  // And the circle still has to reach the furthest corner from there.
  assert.equal(Math.round(page.swapRadius()), Math.round(Math.hypot(290, 660)));
  // The clip the reveal plays is the polygon pair (browser parity: motion.js
  // swapEdgePolygon) in the particle style: equal vertex counts so the two end states
  // interpolate, and the full ring still clears the furthest corner.
  const parse = (poly) => [...poly.matchAll(/([\d.-]+)% ([\d.-]+)%/g)]
    .map((m) => ({ x: (parseFloat(m[1]) / 100) * 480, y: (parseFloat(m[2]) / 100) * 700 }));
  const from = parse(page.swapClip('from'));
  const to = parse(page.swapClip('to'));
  assert.equal(from.length, 240);
  assert.equal(to.length, 240);
  assert.ok(from.every((p) => Math.hypot(p.x - 290, p.y - 40) < 0.5), 'collapsed at the origin');
  const radii = to.map((p) => Math.hypot(p.x - 290, p.y - 40));
  const R = Math.hypot(290, 660);
  assert.ok(radii.every((r) => r >= R - 0.05), 'every vertex clears the furthest corner');
  assert.ok(Math.max(...radii) - Math.min(...radii) < 0.1, 'dust: a perfect circle');
});

test('water and fire cut their own front, and the classic-script twins match dustCloud.js', async () => {
  const dc = await import('../src/lib/dustCloud.js');
  const page = loadAccent();
  const parse = (poly) => [...poly.matchAll(/([\d.-]+)% ([\d.-]+)%/g)]
    .map((m) => Math.hypot((parseFloat(m[1]) / 100) * 480 - 290, (parseFloat(m[2]) / 100) * 700 - 40));
  const R = Math.hypot(290, 660);
  for (const style of [dc.STYLE_WATER, dc.STYLE_FIRE]) {
    const radii = parse(page.motion.edgePolygon(290, 40, 480, 700, 1, style));
    assert.equal(radii.length, 240);
    assert.ok(radii.every((r) => r >= R - 0.05), 'coverage still rules');
    assert.ok(Math.max(...radii) - Math.min(...radii) > R * 0.03, `style ${style}: visibly not a circle`);
    for (const k of [0, 17, 101, 239])
      assert.ok(Math.abs(page.motion.edgeJitter(style, k) - dc.edgeJitter(style, k)) < 1e-12, `edgeJitter ${style}/${k}`);
    for (const w of [0.1, 0.37, 0.8, 0.95]) assert.equal(page.motion.grainShape(style, w), dc.grainShape(style, w));
  }
  assert.equal(page.motion.grainShape(0, 0.8), dc.SHAPE_DISC);
  const norm = (a) => Array.from(a, (v) => +(+v).toFixed(9) + 0);
  assert.deepEqual(norm(page.motion.shapePolygon(dc.SHAPE_TRIANGLE, 10, 20, 2, 0.5)),
                   norm(dc.shapePolygon(dc.SHAPE_TRIANGLE, 10, 20, 2, 0.5)));
  assert.deepEqual(norm(page.motion.shapePolygon(dc.SHAPE_WAVE, 3, 4, 1.5, 2)), norm(dc.shapePolygon(dc.SHAPE_WAVE, 3, 4, 1.5, 2)));
  assert.deepEqual(norm(page.motion.shapePolygon(dc.SHAPE_STREAK, 3, 4, 1.5, 2)), norm(dc.shapePolygon(dc.SHAPE_STREAK, 3, 4, 1.5, 2)));
  // …and so does the tint a grain's hash gives it, and the palette that names it.
  assert.deepEqual([...page.motion.paletteCss()], dc.paletteCss());
  for (let w = 0; w < 1; w += 0.0037) {
    assert.equal(page.motion.tintOf(w), dc.tintOf(w), `tintOf ${w}`);
    assert.equal(page.motion.stopOfTint(0.5, dc.tintOf(w)), dc.stopOfTint(0.5, dc.tintOf(w)), `stopOfTint ${w}`);
  }
});

test('with nothing to anchor to, the wipe blooms from the centre — never a click', () => {
  const page = wipePage({ controls: [] });   // a page with no #theme-toggle at all
  page.theme.set('dark');
  assert.deepEqual(page.swapOrigin(), { x: 240, y: 350 }, 'the viewport centre');
});

test('the caller can hand over the control it owns (the options page has no toggle)', () => {
  const page = wipePage({ controls: [] });
  const select = { getBoundingClientRect: () => ({ left: 100, top: 200, width: 200, height: 30 }) };
  page.theme.set('dark', select);
  assert.deepEqual(page.swapOrigin(), { x: 200, y: 215 });
  const trigger = { getBoundingClientRect: () => ({ left: 10, top: 400, width: 100, height: 20 }) };
  page.accent.set('crimson', trigger);
  assert.deepEqual(page.swapOrigin(), { x: 60, y: 410 });
});

test('a laid-out but invisible copy of the control is skipped for the visible one', () => {
  // A panel that clones its toolbar duplicates ids; an opacity-0 / off-screen copy keeps a
  // perfectly good rect, and blooming from it is exactly how the circle ends up in a corner.
  const page = wipePage({
    controls: [
      { id: 'theme-toggle', rect: { left: 0, top: 0, width: 28, height: 28 }, visible: false },
      { id: 'theme-toggle', rect: { left: -400, top: 10, width: 28, height: 28 } },   // off-screen
      TOGGLE,
    ],
  });
  page.theme.set('dark');
  assert.deepEqual(page.swapOrigin(), { x: 290, y: 40 });
});

// ── Dust in the wipe's wake ──────────────────────────────────────────────────
// The growing circle kicks up specks (browser parity: motion.js swapDustSpecs). They
// spawn only once the transition is `ready` — same frame the ring's own clock starts —
// and are painted in the palette read BEFORE the swap, the paint the front grinds away.
test('the wipe seeds a dust layer on <body> once ready, in the OLD palette', async () => {
  const page = wipePage();
  page.theme.set('dark');
  await Promise.resolve();   // let `ready` deliver
  await Promise.resolve();
  assert.equal(page.bodyChildren.length, 1, 'one dust layer');
  const stage = page.bodyChildren[0];
  assert.equal(stage.className, 'swap-dust');
  assert.equal(stage.tagName, 'CANVAS', 'one stage, not a div per grain');
  // Sized in device pixels, laid out in CSS ones — a hi-dpi wake is not a blurry one.
  assert.deepEqual([stage.style.width, stage.style.height], ['480px', '700px']);

  // Mid-wake: grains of the accent AND its shade are in the air.
  assert.ok(page.frame(140), 'the wake is running');
  const lit = stage.fills.filter((f) => f.arcs > 0);
  const grains = lit.reduce((n, f) => n + f.arcs, 0);
  assert.ok(grains > 10, `a real field of grains (got ${grains})`);
  // Painted from the accent palette, resolved BEFORE the swap (the sandbox cannot compute
  // a colour, so color-mix() strings come through): every fourth grain wears the shade.
  assert.ok(lit.some((f) => f.colour === 'color-mix(in srgb, var(--accent) 100%, var(--accent-2))'), 'accent grains');
  assert.ok(lit.some((f) => f.colour === 'color-mix(in srgb, var(--accent) 0%, var(--accent-2))'), 'shade grains');
  // The whole point of the stage: batched fills — one per (stop, alpha step), each in
  // chunks of 32 grains (lib/dustCloud.js FILL_CHUNK) — not one per grain.
  assert.ok(stage.fills.length <= 6 * 8 + Math.ceil(grains / 32), `batched into ${stage.fills.length} fills, not ${grains}`);
  assert.ok(lit.every((f) => f.arcs <= 32), 'no path longer than a chunk');
  assert.ok(lit.every((f) => f.alpha > 0 && f.alpha <= 1), 'every batch carries its own alpha');

  // Nothing is alight before the ring starts moving, or after the last grain burns out.
  stage.fills.length = 0;
  assert.ok(page.frame(0));
  assert.equal(stage.fills.length, 0, 'no grain ignites at t=0 — the ring is still a point');
  assert.ok(page.frame(280 + 340 + 1) === true);
  assert.ok(stage.removed, 'the stage clears itself off the page');
});

test('the fallback path (no View Transitions) spawns no dust — there is no ring to ride', async () => {
  const page = loadAccent({ controls: [TOGGLE] });
  page.theme.set('dark');
  await Promise.resolve();
  await Promise.resolve();
  assert.equal(page.bodyChildren.length, 0);
});

test('the accent swap anchors to the toggle too, and a cross-page change animates', () => {
  const page = wipePage();
  page.accent.set('sky');
  assert.deepEqual(page.swapOrigin(), { x: 290, y: 40 });
  // A change made in ANOTHER extension page repaints here with no press at all.
  page.store.set('stencil_accent', 'brown');
  page.fireStorage('stencil_accent');
  assert.equal(page.dataAccent(), 'brown');
  assert.deepEqual(page.swapOrigin(), { x: 290, y: 40 });
});

// ── Parity with the two copies that cannot import this file ──

test('ACCENT_HEX in lib/highlightColor.js matches the list exactly', () => {
  const { accent } = loadAccent();
  const fromAccentJs = Object.fromEntries(accent.list.map((a) => [a.key, a.hex]));
  assert.deepEqual(
    fromAccentJs,
    ACCENT_HEX,
    'lib/highlightColor.js ACCENT_HEX drifted from lib/accent.js — both say "keep in sync"',
  );
  assert.equal(DEFAULT_HL, accent.hexOf('violet'), 'the default highlight is the default accent');
});

test('the browser palette (config/accents.json) carries the same keys and hexes', () => {
  const { accent } = loadAccent();
  // The canonical palette now lives in the browser's JSON config; parse it directly.
  const rows = JSON.parse(readFileSync(
    fileURLToPath(new URL('../../browser/js/config/accents.json', import.meta.url)),
    'utf8',
  ));
  const browserAccents = Object.fromEntries(rows.map((a) => [a.key, a.hex.toLowerCase()]));
  assert.equal(Object.keys(browserAccents).length, 12, 'failed to parse browser accents.json');

  const fromAccentJs = Object.fromEntries(accent.list.map((a) => [a.key, a.hex.toLowerCase()]));
  assert.deepEqual(
    fromAccentJs,
    browserAccents,
    'the extension and browser accent palettes drifted',
  );
});

test('the extension and the browser deliberately use DIFFERENT storage keys', () => {
  const { accent } = loadAccent();
  const src = readFileSync(
    fileURLToPath(new URL('../../browser/js/core/accents.js', import.meta.url)),
    'utf8',
  );
  // Different origins, different stores — sharing a key would be the actual bug.
  assert.match(src, /ACCENT_STORAGE_KEY = 'drawingApp_accent'/);
  assert.equal(accent.storageKey, 'stencil_accent');
});

// ── On-accent ink switch ────────────────────────────────────────────────────
// Labels and currentColor line-art sit on --accent, so the accent picks the ink that
// reads on it: whichever of white / near-black contrasts more. accent.js flags
// <html data-accent-light> for the dark one and lib/theme.css swaps --on-accent.
// Mirrors browser/tests/accentController.test.js (same rule).

test('data-accent-light is stamped only for the light presets', () => {
  const { accent, isAccentLight } = loadAccent();
  const light = [];
  for (const a of accent.list) {
    accent.set(a.key);
    if (isAccentLight()) light.push(a.key);
  }
  // The seven presets black reads better on than white does.
  assert.deepEqual(light, ['pink', 'yellow', 'orange', 'aqua', 'sky', 'grass', 'brown']);
});

test('inkOn hands a swatch the ink for its own colour', () => {
  const { accent } = loadAccent();
  assert.equal(accent.inkOn('#eab308'), '#1a1a1a');   // yellow — white 1.92:1, black 10.95:1
  assert.equal(accent.inkOn('#7c3aed'), '#ffffff');   // violet — 5.70 vs 3.69
  assert.equal(accent.inkOn('nope'), '#ffffff');      // not a hex → the white default
});

test('switching from a light accent back to a dark one clears the flag', () => {
  const { accent, isAccentLight } = loadAccent();
  accent.set('yellow');
  assert.equal(isAccentLight(), true);
  accent.set('violet');
  assert.equal(isAccentLight(), false);
});

// ── Interface motion (StencilMotion — browser js/ui/motionPrefs.js twin) ──

test('the motion mode defaults to particles, is stamped before first paint, and mirrors the browser list', async () => {
  const page = loadAccent();
  assert.equal(page.motion.get(), 'particles');
  assert.equal(page.dataMotion(), 'particles', 'data-motion is on <html> at load');
  assert.equal(page.motion.storageKey, 'stencil_motion');
  const browser = await import('../../browser/js/ui/motionPrefs.js');
  // Through JSON: values built inside the vm context carry their own Array prototype.
  const plain = (v) => JSON.parse(JSON.stringify(v));
  assert.deepEqual(plain(page.motion.modes), browser.MOTION_MODES, 'the same five modes, in the same order');
  assert.deepEqual(plain(page.motion.labels), browser.MOTION_MODE_LABELS, 'and the same dropdown labels');
});

test('setting the mode persists it, restamps <html>, and an unknown one reads as particles', () => {
  const page = loadAccent();
  assert.equal(page.motion.set('fire'), 'fire');
  assert.equal(page.store.get('stencil_motion'), 'fire');
  assert.equal(page.dataMotion(), 'fire');
  assert.equal(page.motion.set('sparkles'), 'particles', 'junk falls back');
  assert.equal(page.dataMotion(), 'particles');
  // A stored junk value never silences the page either.
  assert.equal(loadAccent({ stored: { stencil_motion: 'nope' } }).motion.get(), 'particles');
  assert.equal(loadAccent({ stored: { stencil_motion: 'water' } }).dataMotion(), 'water');
  // Private mode: the default stands, nothing throws.
  assert.equal(loadAccent({ storageThrows: true }).motion.set('slide'), 'slide');
});

test('the gates: particles / water / fire fly, slide moves without particles, none is still', () => {
  const page = loadAccent();
  const expect = (mode, reduced, particles, style) => {
    page.motion.set(mode);
    assert.equal(page.motion.reduced(), reduced, `${mode}: reduced`);
    assert.equal(page.motion.particles(), particles, `${mode}: particles`);
    assert.equal(page.motion.style(), style, `${mode}: style`);
  };
  expect('particles', false, true, 'dust');
  expect('water', false, true, 'water');
  expect('fire', false, true, 'fire');
  expect('slide', false, false, null);
  expect('none', true, false, null);
  // The OS preference wins over any stored mode.
  page.motion.set('fire');
  page.setPrefersReduced(true);
  assert.equal(page.motion.reduced(), true);
  assert.equal(page.motion.particles(), false);
  assert.equal(page.motion.style(), null);
});

test('a mode changed on another page lands here through the storage event', () => {
  const page = loadAccent();
  let seen = null;
  page.motion.onChange((m) => { seen = m; });
  page.store.set('stencil_motion', 'water');
  page.fireStorage('stencil_motion');
  assert.equal(page.dataMotion(), 'water', 'the CSS half is restamped');
  assert.equal(seen, 'water', 'and the listener told');
});

test('the classic-script styleFrame is dustCloud.js styleFrame, frame for frame', async () => {
  const { styleFrame } = await import('../src/lib/dustCloud.js');
  const page = loadAccent();
  for (const style of [0, 1, 2]) {
    for (const p of [0, 0.13, 0.5, 0.87, 1]) {
      for (const w of [0, 0.37, 0.91]) {
        for (const t of [0, 133, 777]) {
          // Plain numbers (no -0, no vm prototypes) on both sides.
          const norm = (o) => Object.fromEntries(Object.entries(o).map(([k, v]) => [k, +(+v).toFixed(12) + 0]));
          assert.deepEqual(norm(page.motion.styleFrame(style, p, 1 - p, w, 90, t)),
                           norm(styleFrame(style, p, 1 - p, w, 90, t)), `style ${style} p ${p} w ${w} t ${t}`);
        }
      }
    }
  }
});

test('a water or fire wake is painted from the accent palette; slide keeps the wipe and drops the grain', async () => {
  const styled = loadAccent({ withViewTransitions: true, stored: { stencil_motion: 'fire' } });
  styled.accent.set('sky', 'theme-toggle');
  await Promise.resolve(); await Promise.resolve();
  assert.equal(styled.bodyChildren.length, 1, 'one dust layer');
  const stage = styled.bodyChildren[0];
  styled.frame(0); styled.frame(200);
  const lit = stage.fills.filter((f) => f.arcs > 0);
  assert.ok(lit.length > 0, 'grains were painted');
  // Every fill is a palette colour — a stop of the accent ramp or one of its tints
  // (lib/dustCloud.js paletteCss) — never a pixel of the page.
  const ramp = /^color-mix\(in srgb, var\(--accent\) \d+%, var\(--accent-2\)\)$/;
  const tint = /^(#ffffff|#b4b4b4|#6e6e6e|color-mix\(in srgb, var\(--accent\) 55%, #(ffffff|000000)\))$/;
  for (const f of lit) assert.ok(ramp.test(f.colour) || tint.test(f.colour), `palette fill, got ${f.colour}`);
  assert.ok(lit.some((f) => ramp.test(f.colour)), 'most of it the accent');
  assert.ok(lit.some((f) => tint.test(f.colour)), '…and a tinted minority');
  assert.ok(new Set(lit.map((f) => f.colour)).size >= 2, 'more than one stop of it');

  const sliding = loadAccent({ withViewTransitions: true, stored: { stencil_motion: 'slide' } });
  sliding.accent.set('sky', 'theme-toggle');
  await Promise.resolve(); await Promise.resolve();
  assert.equal(sliding.dataAccent(), 'sky', 'the palette still swaps');
  assert.ok(sliding.swapOrigin(), 'the wipe still plays');
  assert.equal(sliding.bodyChildren.length, 0, 'no wake');

  const still = loadAccent({ withViewTransitions: true, stored: { stencil_motion: 'none' } });
  still.accent.set('sky', 'theme-toggle');
  assert.equal(still.dataAccent(), 'sky');
  assert.equal(still.swapOrigin(), null, 'none: the palette applies outright, no wipe');
});
