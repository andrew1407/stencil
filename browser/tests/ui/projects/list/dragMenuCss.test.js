// The drag-time menu's hover (js/ui/projects/list/dragMenu.js `.is-drag-hover`): a drag fires no
// :hover, so every rule that gives a row-menu item its hover — the look, the glass shimmer, the icon's
// motion, the webcore skin's bar — must name the drag hover beside :hover, with the same body.
import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { LAYOUT_CSS, COMPONENTS_CSS, ANIMATIONS_CSS } from '../../../helpers/css.js';

const webcore = ['menus', 'icons'].map((f) => readFileSync(new URL(`../../../../css/webcore/${f}.css`, import.meta.url), 'utf8'));
const SHEETS = [LAYOUT_CSS, COMPONENTS_CSS, ANIMATIONS_CSS, ...webcore].join('\n');

// Every rule as [selector list, body], comments stripped.
const rules = [...SHEETS.replace(/\/\*[\s\S]*?\*\//g, '').matchAll(/([^{}]+)\{([^{}]*)\}/g)]
  .map((m) => [m[1].trim(), m[2].trim()]);

test('every rule a hovered row-menu item wears covers the drag hover too', () => {
  const hovers = rules.filter(([sel]) => /\.project-menu-item(\.is-danger)?:hover/.test(sel));
  assert.ok(hovers.length >= 5, `found ${hovers.length} hover rules`);
  for (const [sel, body] of hovers) {
    const dragSel = sel.replaceAll(':hover', '.is-drag-hover');
    const covered = rules.some(([s, b]) => b === body && (s.includes('.is-drag-hover') || s.includes(':is(:hover, .is-drag-hover)')))
      || rules.some(([s]) => s === dragSel);
    assert.ok(covered, `no drag twin for: ${sel.replace(/\s+/g, ' ').slice(0, 90)}`);
  }
});

test('the drag hover plays the glass shimmer and the icon motion a real hover plays', () => {
  const shimmer = rules.find(([sel]) => sel.includes('.project-menu-item:hover::after'));
  assert.ok(shimmer[0].includes('.project-menu-item.is-drag-hover::after'), 'the shimmer sweep');
  assert.match(shimmer[1], /animation:\s*ui-shimmer/);
  const motion = rules.find(([sel]) => sel.includes('.project-menu-item.is-drag-hover') && sel.includes(':is(.ic'));
  assert.ok(motion, 'the icon hover motion');
  assert.match(motion[1], /--ic-on:\s*1/);
});
