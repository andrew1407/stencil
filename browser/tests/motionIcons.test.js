// js/ui/motionIcons.js — one glyph per interface-motion mode, and the dropdown that
// wears them (customSelect.js `icons`): the row shows its glyph before the label, the
// trigger the current one, and the CSS plays each mode on hover.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { MOTION_ICONS, motionModeIcon, NONE_LINE_LEN } from '../js/ui/motionIcons.js';
import { MOTION_MODES } from '../js/ui/motionPrefs.js';
import { ANIMATIONS_CSS } from './helpers/css.js';

const read = (p) => readFileSync(new URL(p, import.meta.url), 'utf8');

test('every motion mode has a glyph, and only the modes do', () => {
  assert.deepEqual(Object.keys(MOTION_ICONS).sort(), [...MOTION_MODES].sort());
  for (const m of MOTION_MODES) {
    const svg = motionModeIcon(m);
    assert.match(svg, new RegExp(`^<svg class="mm-icon mm-${m}" viewBox="0 0 16 16"`), m);
    assert.match(svg, /aria-hidden="true"/);
  }
  assert.equal(motionModeIcon('sparkles'), '', 'a stale key shows nothing');
  // The animated parts carry the classes the CSS keys on.
  assert.match(MOTION_ICONS.none, /<circle[^>]*\/><line class="mm-line"[^>]*x1="12\.05" y1="3\.95" x2="3\.95" y2="12\.05"/, 'the line runs top-right → bottom-left');
  assert.match(MOTION_ICONS.none, new RegExp(`stroke-dasharray="${NONE_LINE_LEN}"`));
  assert.ok(Math.abs(Math.hypot(12.05 - 3.95, 12.05 - 3.95) - NONE_LINE_LEN) < 0.1, 'the dash is the line');
  assert.match(MOTION_ICONS.slide, /<path class="mm-shaft" d="M4\.5 11\.5 L11\.5 4\.5" stroke-dasharray="9\.9"\/><path class="mm-head"/, 'the shaft runs bottom-left → top-right, then the head');
  assert.match(MOTION_ICONS.water, /class="mm-drop"/);
  assert.match(MOTION_ICONS.fire, /class="mm-flame"/);
  assert.equal((MOTION_ICONS.particles.match(/class="mm-mote"/g) || []).length, 5);
});

test('the CSS plays each mode on hover, in the direction asked for', () => {
  const css = ANIMATIONS_CSS;
  assert.match(css, /\.accent-dd-opt:hover \.mm-line, \.accent-dd-trigger:hover \.mm-line, \.mm-play \.mm-line \{ animation: mmLineDraw/);
  assert.match(css, /@keyframes mmLineDraw \{ from \{ stroke-dashoffset: 11\.4; \} to \{ stroke-dashoffset: 0; \} \}/, 'drawn from the top-right end');
  assert.match(css, /@keyframes mmShaft \{ from \{ stroke-dashoffset: 9\.9; \} to \{ stroke-dashoffset: 0; \} \}/, 'drawn from the bottom-left end');
  assert.match(css, /@keyframes mmHead \{ 0%, 65% \{ opacity: 0; \} 100% \{ opacity: 1; \} \}/, 'the head appears last');
  assert.match(css, /@keyframes mmDrop \{\s*0%\s*\{ transform: translateY\(-9px\)/, 'the drop falls in from the top');
  assert.match(css, /\.mm-flame \{ transform-box: fill-box; transform-origin: 50% 100%; \}/, 'the flame grows from its base');
  assert.match(css, /@keyframes mmDust \{ from \{ transform: translateY\(-7px\); opacity: 0; \}/, 'the specks settle from the top');
  assert.match(css, /\.mm-mote:nth-of-type\(5\) \{ animation-delay: 0\.36s; \}/, 'one after another');
  // The drop and flame take their time: 1.5x the line and arrow (user decision). The
  // SPECKS run 1.5x faster again (user decision) — 0.825s each, 90ms apart — which is
  // exactly what the desktop's motionIconMs already paints them at.
  assert.match(css, /\.mm-drop \{ animation: mmDrop 1\.125s/);
  assert.match(css, /\.mm-flame \{ animation: mmFlame 0\.9s/);
  assert.match(css, /\.mm-mote \{ animation: mmDust 0\.825s/);
  // The trigger's glyph plays too: on hover, and once when the value just changed.
  for (const part of ['mm-line', 'mm-shaft', 'mm-head', 'mm-drop', 'mm-flame', 'mm-mote'])
    assert.match(css, new RegExp(`\\.accent-dd-opt:hover \\.${part}, \\.accent-dd-trigger:hover \\.${part}, \\.mm-play \\.${part} \\{ animation:`), part);
  // The extension keeps the same rules.
  const ext = read('../../extension/src/lib/animations.css');
  for (const k of ['mmLineDraw', 'mmShaft', 'mmHead', 'mmDrop', 'mmFlame', 'mmDust']) assert.ok(ext.includes(`@keyframes ${k}`), `extension ${k}`);
});

test('the Visuals dropdown wears the glyphs; the dropdown puts them before the label and on the trigger', () => {
  assert.match(read('../js/ui/visualsModal.js'), /enhanceSelect\(motionMode, \{ icons: motionModeIcon \}\)/);
  const cs = read('../js/ui/customSelect.js');
  assert.match(cs, /export function enhanceSelect\(selectEl, \{ search = false, icons = null, preview = null \} = \{\}\)/);
  assert.match(cs, /slot\.className = 'cs-opt-icon';/);
  assert.match(cs, /const glyph = icons \? icons\(selectEl\.value\) : '';/);
  assert.match(cs, /if \(svg && changed\) \{\s*svg\.classList\.add\('mm-play'\);/, 'a changed value plays its glyph in');
  assert.match(read('../../extension/src/options/options.js'), /icons: el\.id === 'motion' \? motionModeIcon : null/);
});

test('a pick is applied before the list leaves and the value swaps, so both play in the new mode', () => {
  const cs = read('../js/ui/customSelect.js');
  const body = cs.slice(cs.indexOf('const choose = (v) => {'), cs.indexOf('trigger.addEventListener'));
  const at = (needle) => body.indexOf(needle);
  assert.ok(at('proto.set.call(selectEl, v);') >= 0, 'the raw setter — no early sync');
  assert.ok(at("dispatchEvent(new Event('change'") < at('close();'), 'change handlers run before the exit');
  assert.ok(at('close();') < at('sync();'), '…and before the trigger swaps its value');
});
