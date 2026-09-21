// Tests for src/lib/logoDragMenu.js — the header logo's spring-loaded drag menu
// extracted from popup.js: the spring dwell, drop-only items, the once-per-release
// gate, the grace countdown, and the file-MIME menu hint.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { fileEntryHint, createLogoDragMenu, LOGO_DROP_HINT } from '../../../src/lib/accent/logoDragMenu.js';
import { stubDoc, stubEl as sharedEl } from '../../helpers/domStub.js';

const tick = (ms) => new Promise((r) => setTimeout(r, ms));

// ── fileEntryHint ──

test('fileEntryHint reads a dragged FILE\'s MIME mid-drag: video, image, or nothing', () => {
  assert.deepEqual(fileEntryHint({ items: [{ kind: 'file', type: 'video/mp4' }] }),
    { kind: 'video', src: '', videoUrl: 'file:pending' });
  assert.deepEqual(fileEntryHint({ items: [{ kind: 'file', type: 'image/png' }] }),
    { kind: 'img', src: 'file:pending' });
  assert.equal(fileEntryHint({ items: [{ kind: 'string', type: 'text/plain' }] }), null);
  assert.equal(fileEntryHint(null), null);
});

// The brand sits at a known place on screen: the menu is placed off its rect.
const stubEl = () => sharedEl('div', { rect: { left: 20, right: 60, top: 8, bottom: 40 } });

const build = ({ draggingRow = null } = {}) => {
  const doc = stubDoc({ createElement: stubEl });
  const logoEl = stubEl();
  const menuEl = stubEl();
  menuEl.hidden = true;
  const placed = [];
  const actions = [];
  let sharedCloses = 0;
  const menu = createLogoDragMenu({
    logoEl, menuEl,
    placeMenu: (x, y) => placed.push([x, y]),
    closeSharedMenu: () => sharedCloses++,
    dragKind: (e) => e.kind || null,
    getDraggingRow: () => draggingRow,
    onAction: (id, payload) => actions.push([id, payload]),
    springMs: 5,
    graceMs: 10,
    doc,
  });
  const brandTarget = { closest: (sel) => (sel === 'header .logo, header h1' ? logoEl : null) };
  const outsideTarget = { closest: () => null };
  return { doc, logoEl, menuEl, placed, actions, menu, brandTarget, outsideTarget, sharedCloses: () => sharedCloses };
};
const dragEvent = (target, kind = 'url') => ({
  target, kind, relatedTarget: null,
  preventDefault: () => {}, stopPropagation: () => {},
  dataTransfer: { types: ['text/uri-list'], items: [] },
});

// ── Spring open ──

test('dwelling a compatible drag on the brand springs the drop-only menu under the logo', async () => {
  const { doc, menuEl, placed, menu, brandTarget } = build();
  doc.fire('dragover', dragEvent(brandTarget));
  assert.equal(menu.isOpen(), false, 'nothing opens before the dwell');
  await tick(15);
  assert.equal(menu.isOpen(), true);
  assert.equal(menuEl.hidden, false);
  // The four optimistic actions, each a drop target with no click handler.
  assert.deepEqual(menuEl.children.map((b) => b.dataset.action), ['editor', 'newtab', 'incognito', 'crop']);
  for (const b of menuEl.children) {
    assert.equal(b.className, 'drag-item');
    assert.equal(b.handlers.click, undefined, 'drop-ONLY: no click path to the action');
    assert.ok(b.handlers.drop, 'each item accepts the release');
  }
  // Placed under the logo (left edge, bottom + 6).
  assert.deepEqual(placed, [[20, 46]]);
});

test('an incompatible drag or one outside the brand zone never springs', async () => {
  const { doc, menu, brandTarget, outsideTarget } = build();
  doc.fire('dragover', dragEvent(outsideTarget));
  doc.fire('dragover', { ...dragEvent(brandTarget), kind: null });
  await tick(15);
  assert.equal(menu.isOpen(), false);
});

test('an internal drag builds the menu from the dragged row (no crop for a bare video URL)', async () => {
  const row = { kind: 'video', src: '', videoUrl: 'http://x/v.mp4' };
  const { doc, menuEl, menu, brandTarget } = build({ draggingRow: row });
  doc.fire('dragover', dragEvent(brandTarget, 'internal'));
  await tick(15);
  assert.equal(menu.isOpen(), true);
  assert.deepEqual(menuEl.children.map((b) => b.dataset.action), ['newtab'],
    'only the actions that apply to the dragged row are offered');
});

// ── Release on an item ──

test('a drop on an item dispatches exactly once and swallows the stray follow-up click', async () => {
  const { doc, menuEl, actions, menu, brandTarget } = build();
  doc.fire('dragover', dragEvent(brandTarget));
  await tick(15);
  const item = menuEl.children[1];   // newtab
  const drop = {
    preventDefault: () => {}, stopPropagation: () => {},
    dataTransfer: { files: [], getData: (t) => (t === 'text/uri-list' ? 'http://x/a.png' : '') },
  };
  item.fire('drop', drop);
  item.fire('drop', drop);   // a re-dispatched release must not run twice
  assert.deepEqual(actions, [['newtab', { kind: 'url', url: 'http://x/a.png' }]]);
  assert.equal(menu.isOpen(), false, 'closed before dispatching');

  // The gate also suppresses the synthetic click that can follow the drop.
  let swallowed = 0;
  doc.fire('click', {
    target: menuEl, preventDefault: () => swallowed++, stopPropagation: () => {},
  });
  assert.equal(swallowed, 1);
});

test('an unreadable payload closes the menu without dispatching', async () => {
  const { doc, menuEl, actions, brandTarget } = build();
  doc.fire('dragover', dragEvent(brandTarget));
  await tick(15);
  menuEl.children[0].fire('drop', {
    preventDefault: () => {}, stopPropagation: () => {},
    dataTransfer: { files: [], getData: () => '' },
  });
  assert.deepEqual(actions, []);
});

// ── Grace countdown ──

test('leaving the brand closes after the grace; coming back in time cancels it', async () => {
  const { doc, menu, brandTarget, outsideTarget } = build();
  doc.fire('dragover', dragEvent(brandTarget));
  await tick(15);
  assert.equal(menu.isOpen(), true);

  // Leave → grace runs out → closed.
  menu.graceOnDragLeave({ target: brandTarget, relatedTarget: outsideTarget });
  await tick(20);
  assert.equal(menu.isOpen(), false);

  // Again, but return onto the brand before the grace expires.
  doc.fire('dragover', dragEvent(brandTarget));
  await tick(15);
  menu.graceOnDragLeave({ target: brandTarget, relatedTarget: outsideTarget });
  await tick(3);
  doc.fire('dragover', dragEvent(brandTarget));   // re-entered — cancels the countdown
  await tick(20);
  assert.equal(menu.isOpen(), true);
});

// ── End-of-drag paths + the pulse ──

test('release and dismiss close the menu; armUpdate pulses the logo only for usable payloads', async () => {
  const { doc, logoEl, menu, brandTarget } = build();
  doc.fire('dragover', dragEvent(brandTarget));
  await tick(15);
  menu.release();
  assert.equal(menu.isOpen(), false);
  assert.equal(logoEl.classList.contains('drop-over'), false);

  doc.fire('dragover', dragEvent(brandTarget));
  await tick(15);
  menu.dismiss();   // Escape
  assert.equal(menu.isOpen(), false);

  menu.armUpdate(['text/uri-list']);
  assert.equal(logoEl.classList.contains('drag-armed'), true);
  menu.armUpdate(['text/weird']);
  assert.equal(logoEl.classList.contains('drag-armed'), false);
  menu.armUpdate(['Files']);
  assert.equal(logoEl.classList.contains('drag-armed'), true);
  menu.armEnd();
  assert.equal(logoEl.classList.contains('drag-armed'), false);
});

// data-title, not an SVG <title> child and not a title attribute: either of those
// raises Chrome's own popup on top of the custom tooltip (src/lib/tip.js).
test('the logo carries the drop hint as data-title, with no native <title>', () => {
  const { logoEl } = build();
  assert.equal(logoEl.dataset.title, LOGO_DROP_HINT);
  assert.equal(logoEl.children.length, 0, 'no <title> child for the browser to render');
  assert.equal(logoEl.title, '', 'and no title attribute either');
});
