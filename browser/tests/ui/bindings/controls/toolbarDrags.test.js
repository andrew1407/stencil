// Which toolbar icons drag (js/ui/bindings/controls/toolbarDrags.js), over the real toolbar
// markup: the window openers, the chat, the canvas actions and the zoom trio, and no other icon;
// a canvas drop fires rotate and flip as their click does and clears every line without asking.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createStubElement } from '../../../helpers/dom.js';
import { StencilToolbar } from '../../../../js/ui/toolbar/toolbar.js';
import { wireToolbarDrags, wireCloneDrags, canvasActs, CANVAS_CLICK_IDS } from '../../../../js/ui/bindings/controls/toolbarDrags.js';
import { windowOpeners } from '../../../../js/ui/drag/modalDrag.js';

const buttonIds = [...StencilToolbar.inner().matchAll(/<button id="([\w-]+)"/g)].map((m) => m[1]);

const barRig = () => {
  const els = new Map(buttonIds.map((id) => [id, createStubElement('button', { id })]));
  return { els, bar: { $: (id) => els.get(id) ?? null } };
};
const app = (calls = []) => ({
  canvas: null, chat: null, zoomPan: {},
  clearAllLines: (opts) => calls.push(['clearAllLines', opts]),
});

test('exactly the window openers, the chat, the canvas actions and the zoom trio drag', () => {
  const { els, bar } = barRig();
  const wired = wireToolbarDrags(app(), bar, {});
  const openers = windowOpeners().map(([id]) => id).filter((id) => els.has(id));
  const expected = [...CANVAS_CLICK_IDS, 'clear-all-lines', 'chat-btn', ...openers, 'zoom-in', 'zoom-out', 'zoom-fit'];
  assert.deepEqual([...wired].sort(), [...expected].sort());
  for (const id of ['projects-btn', 'connect-btn', 'crop-image', 'load-image-btn', 'open-image-btn', 'info-btn']) {
    assert.ok(wired.includes(id), `${id} opens its window where it lands`);
  }
  const dragging = buttonIds.filter((id) => els.get(id).listeners.pointerdown?.length);
  assert.deepEqual(dragging.sort(), [...expected].sort(), 'no other toolbar icon drags');
});

test('every id the wiring names is a toolbar button', () => {
  for (const id of [...CANVAS_CLICK_IDS, 'clear-all-lines', 'chat-btn', 'zoom-in', 'zoom-out', 'zoom-fit']) {
    assert.ok(buttonIds.includes(id), id);
  }
});

test('a canvas drop fires rotate and flip as their click, and clears the lines without asking', () => {
  const { els, bar } = barRig();
  const calls = [];
  for (const id of CANVAS_CLICK_IDS) els.get(id).click = () => calls.push(['click', id]);
  for (const [, act] of canvasActs(app(calls), bar)) act();
  assert.deepEqual(calls, [
    ['click', 'rotate-left'], ['click', 'rotate-right'], ['click', 'flip-horizontal'],
    ['clearAllLines', { ask: false }],
  ]);
});

test('without a toolbar nothing is wired', () => {
  assert.deepEqual(wireToolbarDrags(app(), null), []);
  assert.deepEqual(wireCloneDrags(app(), null), []);
});

test("the fullscreen strip's copy drags as the toolbar does, the theme switch with it", () => {
  const { els, bar } = barRig();
  const expected = wireToolbarDrags(app(), bar, {});
  const copy = barRig();
  const root = { querySelector: (sel) => copy.els.get(sel.slice(1)) ?? null };
  const wired = wireCloneDrags(app(), root);
  assert.deepEqual([...wired].sort(), [...expected, 'theme-toggle'].sort());
  assert.ok(copy.els.get('theme-toggle').listeners.pointerdown?.length, 'the lens drags from the copy');
  assert.ok(!els.get('theme-toggle').listeners.pointerdown?.length, 'the original is left alone');
});
