// Who asks which motion gate (js/ui/motion/motionPrefs.js), and where the switches live: every
// cloud behind the dust gate and the strokes behind the drawing one, the pre-paint script run
// for real, and the Visuals modal, console facade and settings controller wired end to end.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import vm from 'node:vm';
import { createStubElement } from '../../helpers/dom.js';
import { ANIMATIONS_CSS } from '../../helpers/css.js';
import { installVisualsDom, mountVisuals, element, frames } from '../../helpers/visualsModalRig.js';

const doc = installVisualsDom();
globalThis.MutationObserver = class { observe() {} };
// Colours resolve to what they were given (resolveColour's probe), so a palette reads back as itself.
globalThis.getComputedStyle = (el) => ({ getPropertyValue: (n) => (n === '--accent' ? '#7c3aed' : ''), color: el?.style?.color ?? '' });
const read = (p) => readFileSync(new URL(p, import.meta.url), 'utf8');
const prefs = await import('../../../js/ui/motion/motionPrefs.js');
const { setMotionPrefs, motionPrefs, MOTION_MODES, MOTION_STORAGE_KEY, DEFAULT_MOTION_MODE } = prefs;
const { disintegrate, ghostOut, ghostIn, spawnSwapDust, swapDustPaint } = await import('../../../js/ui/motion.js');
const { PARTICLE_STYLES, paletteCss } = await import('../../../js/ui/dust/cloud.js');
const { attachVoiceDust } = await import('../../../js/ui/dust/voiceDust.js');
const { StrokeFx } = await import('../../../js/core/line/strokeFx.js');

// Every 2D call and fill colour, in order, across the canvases the page makes.
const ops = [];
const ctx = new Proxy({}, {
  get: (t, k) => (k in t ? t[k] : k === 'getImageData' ? (x, y, w, h) => ({ data: new Uint8ClampedArray(w * h * 4).fill(255) })
    : k === 'createRadialGradient' ? () => ({ addColorStop() {} }) : () => { ops.push(k); }),
  set: (t, k, v) => { if (k === 'fillStyle') ops.push(`fill:${v}`); t[k] = v; return true; },
});
doc.createElement = (tag) => {
  const el = element(tag);
  if (tag === 'canvas') el.getContext = () => ctx;
  return el;
};
const sized = (el, w, h) => Object.assign(el, { getBoundingClientRect: () => ({ left: 0, top: 0, width: w, height: h, right: w, bottom: h }) });
const picture = () => {
  const host = sized(element('div'), 400, 300);
  const canvas = sized(doc.createElement('canvas'), 200, 100);
  Object.assign(canvas, { width: 200, height: 100 });
  host.appendChild(canvas);
  return canvas;
};
const runFrames = () => frames.splice(0).forEach((fn) => fn(performance.now() + 120));

test('every cloud in the app is built behind the dust gate, and the strokes behind the drawing one', () => {
  for (const [mode, flies] of [['particles', true], ['water', true], ['fire', true], ['slide', false], ['none', false]]) {
    setMotionPrefs({ mode });
    const el = sized(createStubElement('div'), 200, 100);
    assert.equal(disintegrate(el, {}), flies, `${mode}: the element cloud`);
    if (flies) {
      assert.equal(el.__dustHost.__cloud.style, PARTICLE_STYLES[prefs.showMotionStyle()], `${mode}: it wears the style`);
      assert.deepEqual(el.__dustHost.__cloud.colours, paletteCss(), 'painted from the accent palette');
    }
    const canvas = picture();
    ops.length = 0;
    assert.equal(ghostOut(canvas), flies, `${mode}: the canvas ghost out`);
    assert.equal(ghostIn(picture()), flies, `${mode}: …and in`);
    ops.length = 0;
    frames.length = 0;
    spawnSwapDust({ x: 10, y: 10, w: 400, h: 300 }, swapDustPaint());
    assert.equal(ops.length > 0, flies, `${mode}: the theme wake`);
    const mic = sized(createStubElement('div'), 30, 30);
    const layers = doc.body.children.length;
    attachVoiceDust(mic, () => true);
    runFrames();
    assert.equal(doc.body.children.length > layers, flies, `${mode}: the voice mic's motes and ring`);
  }
  // The canvas ghost paints the style from the palette too: a water grain is not a dust grain.
  const ghostOps = (mode) => { setMotionPrefs({ mode }); frames.length = 0; ops.length = 0; ghostOut(picture()); runFrames(); return ops; };
  const dust = ghostOps('particles');
  assert.ok(paletteCss().some((c) => dust.includes(`fill:${c}`)), 'the ghost fills from the palette');
  assert.ok(dust.includes('arc') && !dust.includes('ellipse'), 'dust grains are discs');
  const water = ghostOps('water');
  assert.ok(water.includes('ellipse') && !water.includes('arc'), 'and a water ghost flies drops');
  // The theme wake reads the palette BEFORE the flip.
  assert.deepEqual(swapDustPaint(), { palette: paletteCss() });
  // The canvas stroke flight answers the drawing switch instead.
  const line = { points: [{ x: 0, y: 0 }, { x: 40, y: 0 }] };
  setMotionPrefs({ mode: 'particles', drawing: false });
  assert.equal(new StrokeFx({}, { schedule: () => 0 }).flyIn(line, 1), null, 'drawing off: the vertex lands at once');
  setMotionPrefs({ mode: 'slide', drawing: true });
  assert.ok(new StrokeFx({}, { schedule: () => 0 }).flyIn(line, 1), 'drawing on flies, whatever the interface mode');
  setMotionPrefs({ mode: 'particles' });
  // One gate, asked in one place: no component still reads the media query by hand (a lint).
  for (const f of ['../../../js/ui/modal/flight.js', '../../../js/ui/toolbar/toolbar.js', '../../../js/ui/motion.js'])
    assert.ok(!read(f).includes("matchMedia('(prefers-reduced-motion: reduce)')"), f);
});

// The classic <head> script, run for real against a stub <html> and store.
const prePaint = (stored) => {
  const attrs = new Map();
  vm.runInNewContext(read('../../../js/prePaintTheme.js'), {
    document: { documentElement: { setAttribute: (k, v) => attrs.set(k, v) } },
    window: { matchMedia: () => ({ matches: false }) },
    localStorage: { getItem: (k) => stored[k] ?? null },
  });
  return attrs.get('data-motion');
};

test('the mode reaches the CSS before first paint, and stops what CSS alone drives', () => {
  for (const mode of MOTION_MODES)
    assert.equal(prePaint({ [MOTION_STORAGE_KEY]: JSON.stringify({ mode }) }), mode, `the same key and list as the module: ${mode}`);
  for (const junk of ['{ not json', JSON.stringify({ mode: 'sparkles' }), undefined])
    assert.equal(prePaint({ [MOTION_STORAGE_KEY]: junk }), DEFAULT_MOTION_MODE);
  const css = ANIMATIONS_CSS;
  assert.match(css, /:root\[data-motion="none"\] \*,/);
  assert.match(css, /animation-duration: 0\.01ms !important;/);
  // 'slide' needs no rules of its own — each surface keeps its own entrance — except the
  // chat entry, whose only entrance ever WAS its dust.
  assert.match(css, /\.chat-slide-in \{ animation: chatRiseIn/);
});

test('every switch is in the Visuals modal and on the console facade', async () => {
  const { visualsModalInner } = await import('../../../js/ui/visuals/markup.js');
  const markup = visualsModalInner();
  assert.match(markup, /<div class="vs-section">Motion<\/div>/);
  for (const id of ['vs-draw-anim', 'vs-modal-backdrop', 'vs-motion-mode']) assert.match(markup, new RegExp(`id="${id}"`));
  // The checkboxes are one table — id ↔ motionPrefs key — read on open, written on change.
  setMotionPrefs({ mode: 'water', drawing: false, backdrop: true });
  const { calls, overlay } = await mountVisuals(doc, MOTION_MODES);
  overlay.__stencilModal.open();
  const $ = (id) => doc.getElementById(id);
  assert.deepEqual([$('vs-draw-anim').checked, $('vs-modal-backdrop').checked, $('vs-motion-mode').value], [false, true, 'water']);
  $('vs-modal-backdrop').checked = false;
  $('vs-modal-backdrop').dispatch('change');
  $('vs-motion-mode').value = 'fire';
  $('vs-motion-mode').dispatch('change');
  assert.deepEqual(calls, [['backdrop', false], ['mode', 'fire']]);
  // Reset All restores every one of them along with the colours, from the same table.
  calls.length = 0;
  $('vs-reset').dispatch('click');
  assert.deepEqual(calls, [['drawing', true], ['backdrop', true], ['mode', 'particles']]);
  // The facade's settings namespace goes through the ONE setter.
  const { createSettingsFacade } = await import('../../../js/console/settingsFacade.js');
  const setMotion = [];
  const facade = createSettingsFacade({ app: { settings: { setMotion: (k, v) => setMotion.push([k, v]) } }, guard: (o) => o }).settings();
  assert.equal(facade.drawingAnimations, motionPrefs().drawing);
  facade.modalBackdrop = 0;
  facade.motionMode = 'slide';
  assert.deepEqual(setMotion, [['backdrop', false], ['mode', 'slide']]);
  // …which is also what rejects a bad mode, and paints the modal's own control.
  const { SettingsController } = await import('../../../js/core/settings/controller.js');
  const controller = new SettingsController({});
  assert.throws(() => controller.setMotion('mode', 'sparkles'), /Unknown motion mode/);
  const mirror = doc.register('vs-motion-mode', createStubElement('select'));
  assert.equal(controller.setMotion('mode', ' Fire ').mode, 'fire');
  assert.equal(mirror.value, 'fire', 'settingMirrors paints the select');
  setMotionPrefs({ mode: 'particles', drawing: true, backdrop: true });
});
