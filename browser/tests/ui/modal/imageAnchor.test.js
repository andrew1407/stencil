// The open-image flow's two flight rules (js/ui/modal/imageAnchor.js), which the desktop mirrors:
// a confirm about opening an image grows out of the CANVAS CENTRE however it was raised — a drop
// lands anywhere — and an answer that OPENS an image pours into the toolbar's Open control, while a
// cancel goes back to the canvas. Nothing here changes gestureAnchorRect: other confirms still fly
// out of the press that raised them.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createStubElement, installDom } from '../../helpers/dom.js';

const IDS = ['confirm-modal-close', 'confirm-modal-cancel', 'confirm-modal-confirm',
             'confirm-modal-title-text', 'confirm-modal-title-icon', 'confirm-modal-message',
             'confirm-modal-confirm-text', 'confirm-modal-cancel-text'];

const rectOf = (r) => () => ({ ...r, right: r.left + r.width, bottom: r.top + r.height });
const BOX = { left: 400, top: 300, width: 200, height: 150 };
const VIEWPORT = { left: 100, top: 100, width: 600, height: 400 };
const TOOLBAR = { left: 820, top: 12, width: 28, height: 24 };
// A 40px box on the viewport's centre — imageAnchor.js IMAGE_ANCHOR_PX.
const CANVAS_HOME = { left: 380, top: 280, right: 420, bottom: 320, width: 40, height: 40 };

const box = createStubElement('div', { getBoundingClientRect: rectOf(BOX) });
const bodyEl = createStubElement('div');
const overlay = createStubElement('div', {
  querySelector: (sel) => (sel === '.app-modal' ? box : sel === '.settings-body' ? bodyEl : null),
});
const footer = createStubElement('div');

const doc = installDom({
  // askAlt builds its third button as markup, then reaches back in for the label span.
  createElement: (tag) => {
    const span = createStubElement('span');
    return createStubElement(tag, { focus() {}, querySelector: (s) => (s === 'span' ? span : null) });
  },
}, {
  window: { matchMedia: () => ({ matches: false }), innerWidth: 1000, innerHeight: 800,
            addEventListener() {}, removeEventListener() {} },
  matchMedia: () => ({ matches: false }),
});
doc.register('confirm-modal-overlay', overlay);
for (const id of IDS) doc.register(id, createStubElement('button', { id }));
footer.appendChild(doc.getElementById('confirm-modal-cancel'));
const viewportEl = doc.register('canvas-viewport', createStubElement('div', { getBoundingClientRect: rectOf(VIEWPORT) }));
// The toolbar pair: #open-image-btn shows once an image is open, #load-image-btn before that
// (ui/control/state.js swaps them), and a hidden one measures 0x0 exactly as the browser reports.
const GONE = rectOf({ left: 0, top: 0, width: 0, height: 0 });
const openBtn = doc.register('open-image-btn', createStubElement('button', { getBoundingClientRect: rectOf(TOOLBAR) }));
const loadBtn = doc.register('load-image-btn', createStubElement('button', { getBoundingClientRect: GONE }));

const { setMotionPrefs } = await import('../../../js/ui/motion/motionPrefs.js');
setMotionPrefs({ mode: 'slide' });   // every flight plays, none of them out of real particles
const { StencilConfirmModal } = await import('../../../js/ui/modal/confirmModal.js');
const { canvasAnchorRect, openImageAnchorRect, openImageConfirmAnchors, emptiedControlRect } =
  await import('../../../js/ui/modal/imageAnchor.js');
const { wireModalShell } = await import('../../../js/ui/base.js');

// Mirrors createModalFlight.setOrigin (js/ui/modal/flight.js), so the expectation is computed the
// same way the code computes it rather than re-typed as magic numbers.
const expectedVars = (anchor) => {
  const onScreen = !!anchor && anchor.width > 0 && anchor.height > 0 && anchor.bottom > 0 && anchor.top < 800;
  const cx = onScreen ? anchor.left + anchor.width / 2 : BOX.left + BOX.width / 2;
  const cy = onScreen ? anchor.top + anchor.height / 2 : -Math.max(48, BOX.height * 0.3);
  return {
    dx: `${Math.round(cx - (BOX.left + BOX.width / 2))}px`,
    dy: `${Math.round(cy - (BOX.top + BOX.height / 2))}px`,
    sx: String(onScreen ? Math.max(anchor.width / BOX.width, 0.05) : 0.4),
    sy: String(onScreen ? Math.max(anchor.height / BOX.height, 0.05) : 0.4),
  };
};
const readVars = () => ({
  dx: box.style.getPropertyValue('--modal-dx'), dy: box.style.getPropertyValue('--modal-dy'),
  sx: box.style.getPropertyValue('--modal-sx'), sy: box.style.getPropertyValue('--modal-sy'),
});
// Where the user last pressed: far from both the canvas centre and the toolbar.
const pressFarAway = () => doc.dispatch('pointerdown', { clientX: 900, clientY: 700 });

const modal = new StencilConfirmModal();
modal.wire();

test('the canvas anchor is a small box on the viewport centre, and the toolbar anchor the shown half', () => {
  assert.deepEqual(canvasAnchorRect(), CANVAS_HOME);
  assert.deepEqual(openImageAnchorRect(), openBtn.getBoundingClientRect(), 'an image is open: the ⧉ icon');

  openBtn.getBoundingClientRect = GONE;          // no image yet: #image-actions is hidden…
  loadBtn.getBoundingClientRect = rectOf(TOOLBAR);
  assert.deepEqual(openImageAnchorRect(), loadBtn.getBoundingClientRect(), '…so the "Open Image" button answers');
  openBtn.getBoundingClientRect = rectOf(TOOLBAR);
  loadBtn.getBoundingClientRect = GONE;

  const prev = viewportEl.getBoundingClientRect;
  viewportEl.getBoundingClientRect = GONE;       // nothing laid out: the window's own centre stands in
  assert.deepEqual(canvasAnchorRect(), { left: 480, top: 380, right: 520, bottom: 420, width: 40, height: 40 });
  assert.deepEqual(openImageAnchorRect(), openBtn.getBoundingClientRect(), 'the toolbar is unaffected');
  viewportEl.getBoundingClientRect = prev;
});

test('an open-image confirm flies from the canvas centre, not from the drop point', () => {
  pressFarAway();
  const gesture = { left: 887, top: 687, right: 913, bottom: 713, width: 26, height: 26 };
  modal.askAlt('An image is already open. Where should the dropped image open?',
    { altLabel: 'Open in a new page', ...openImageConfirmAnchors() });

  assert.deepEqual(readVars(), expectedVars(CANVAS_HOME), 'it grows out of the canvas');
  assert.notDeepEqual(readVars(), expectedVars(gesture), 'and emphatically not out of the press');
  doc.getElementById('confirm-modal-cancel').dispatch('click');   // leave nothing up for the next case
});

test('cancelling it pours back into the canvas centre, since no image was opened', async () => {
  pressFarAway();
  const answer = modal.askAlt('An image is already open. Where should the dropped image open?',
    { altLabel: 'Open in a new page', ...openImageConfirmAnchors() });
  doc.getElementById('confirm-modal-cancel').dispatch('click');

  assert.equal(await answer, null, 'Cancel is the way out');
  assert.deepEqual(readVars(), expectedVars(CANVAS_HOME));
  assert.notDeepEqual(readVars(), expectedVars(openBtn.getBoundingClientRect()));
});

test('an answer that opens an image pours into the toolbar Open control instead', async () => {
  pressFarAway();
  const answer = modal.askAlt('An image is already open. Where should the dropped image open?',
    { altLabel: 'Open in a new page', ...openImageConfirmAnchors() });
  doc.getElementById('confirm-modal-confirm').dispatch('click');

  assert.equal(await answer, 'confirm');
  assert.deepEqual(readVars(), expectedVars(openBtn.getBoundingClientRect()));
});

test('the third button opens an image too, so it lands on the same control', async () => {
  const answer = modal.askAlt('An image is already open. Where should the dropped image open?',
    { altLabel: 'Open in a new page', ...openImageConfirmAnchors() });
  footer.children.find((c) => c.id === 'confirm-modal-alt').dispatch('click');

  assert.equal(await answer, 'alt');
  assert.deepEqual(readVars(), expectedVars(openBtn.getBoundingClientRect()));
});

test('a confirm with no anchor named still flies out of the gesture that raised it', () => {
  pressFarAway();
  modal.ask('Clear this editor (image + lines)?', { danger: true });
  assert.deepEqual(readVars(), expectedVars({ left: 887, top: 687, right: 913, bottom: 713, width: 26, height: 26 }),
    'gestureAnchorRect is untouched for every other confirm');
  doc.getElementById('confirm-modal-cancel').dispatch('click');
});

// Rule 2 for the Open Image WINDOW: "＋ Blank image" on the idle canvas opens it, and that card is
// gone the moment the blank exists — the window used to collapse into the canvas (shell.js's
// fallback), and now openImage/modal.js hands close() the toolbar control.
test('the Open Image window collapses into the toolbar control when its outcome opened an image', (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  const shellBox = createStubElement('div', { getBoundingClientRect: rectOf(BOX) });
  const shellOverlay = createStubElement('div', {
    querySelector: (sel) => (sel === '.app-modal' ? shellBox : null),
  });
  const blankCard = createStubElement('button', { getBoundingClientRect: rectOf({ left: 300, top: 250, width: 160, height: 120 }) });
  const shell = wireModalShell(shellOverlay, null, null);
  const readShell = () => ({
    dx: shellBox.style.getPropertyValue('--modal-dx'), dy: shellBox.style.getPropertyValue('--modal-dy'),
    sx: shellBox.style.getPropertyValue('--modal-sx'), sy: shellBox.style.getPropertyValue('--modal-sy'),
  });

  shell.open(blankCard);
  blankCard.getBoundingClientRect = GONE;   // the idle card goes with the first image
  shell.close(openImageAnchorRect());
  assert.deepEqual(readShell(), expectedVars(openBtn.getBoundingClientRect()));

  // Cancel names no target, the card is still gone — the canvas centre is the fallback (rule 1).
  shell.open(blankCard);
  shell.close();
  assert.deepEqual(readShell(), expectedVars(CANVAS_HOME));
});

// Both anchors are read BEFORE the sweep that gets the toolbar there: the aim is the place the
// control lands, so a half still hidden — and a control the shrinking Image section will slide —
// answer with where they will be, not with what measures now (imageAnchor.js settledRect).
test('an anchor is where the control lands, not where the toolbar still has it', async () => {
  const shownRect = (el, r) => () => (el.style.display === 'none' ? { left: 0, top: 0, right: 0, bottom: 0, width: 0, height: 0 } : rectOf(r)());
  openBtn.getBoundingClientRect = shownRect(openBtn, TOOLBAR);
  loadBtn.getBoundingClientRect = shownRect(loadBtn, { left: 20, top: 12, width: 140, height: 34 });
  const actions = doc.register('image-actions', createStubElement('div'));
  // The trash rides the row: the Image section's big button pushes it along when it comes back.
  const trash = doc.register('clear-storage', createStubElement('button', {}));
  trash.getBoundingClientRect = () => rectOf(loadBtn.style.display === 'none'
    ? { left: 900, top: 12, width: 28, height: 24 }
    : { left: 700, top: 12, width: 28, height: 24 })();

  openBtn.style.display = 'none';          // the swap that shows it has not run yet
  loadBtn.style.display = '';
  assert.deepEqual(openImageAnchorRect(), rectOf(TOOLBAR)(), 'the ⧉ icon answers from where it lands');
  assert.equal(openBtn.style.display, 'none', 'and the toolbar is put straight back');

  openBtn.style.display = '';              // …and the other way: an editor about to be emptied
  loadBtn.style.display = 'none';
  assert.deepEqual(emptiedControlRect('clear-storage'), rectOf({ left: 700, top: 12, width: 28, height: 24 })());
  assert.equal(loadBtn.style.display, 'none');
  actions.style.display = '';
  openBtn.getBoundingClientRect = rectOf(TOOLBAR);
  loadBtn.getBoundingClientRect = GONE;
});
