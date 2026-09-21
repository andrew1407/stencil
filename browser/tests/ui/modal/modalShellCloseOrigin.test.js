// A modal opened with NO gesture behind it (open(null) — the projects modal's on-boot auto-chooser)
// still deserves a normal CLOSE, shrinking into the icon that would reopen it. wireModalShell
// (js/ui/base.js) therefore resolves the flight's origin at close() too, not only in open(), where a
// null `originEl` kept --modal-dy at its "fell from above" value for the modal's whole life.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createStubElement, installDom } from '../../helpers/dom.js';

// A rect shaped like a real getBoundingClientRect() result (has `.bottom`, which
// setOrigin's onScreen check reads directly rather than deriving from top+height).
const rectOf = (r) => () => ({ ...r, bottom: r.top + r.height });

installDom({}, {
  // base.js and motion.js ask the SAME gate (ui/prefs.js), so reduced motion is off here and
  // setOriginVars really runs; the particles are switched off through the motion mode instead.
  window: { matchMedia: () => ({ matches: false }), innerWidth: 1000, innerHeight: 800, addEventListener: () => {} },
  matchMedia: () => ({ matches: false }),
});

const { wireModalShell } = await import('../../../js/ui/base.js');
const { setMotionPrefs } = await import('../../../js/ui/motion/motionPrefs.js');
// 'slide': every flight still plays, none of them out of dust — so playDust's particle
// build (surfaceDust/disintegrate, which wants a real document) never runs.
setMotionPrefs({ mode: 'slide' });

// Mirrors createModalFlight's setOrigin (js/ui/base.js) so the expectation is computed
// the same way the code under test computes it, not re-typed as magic numbers.
const expectedVars = (box, anchor) => {
  const onScreen = !!anchor && anchor.width > 0 && anchor.height > 0 && anchor.bottom > 0 && anchor.top < 800;
  const cx = onScreen ? anchor.left + anchor.width / 2 : box.left + box.width / 2;
  const cy = onScreen ? anchor.top + anchor.height / 2 : -Math.max(48, box.height * 0.3);
  return {
    dx: `${Math.round(cx - (box.left + box.width / 2))}px`,
    dy: `${Math.round(cy - (box.top + box.height / 2))}px`,
    sx: String(onScreen ? Math.max(anchor.width / box.width, 0.05) : 0.4),
    sy: String(onScreen ? Math.max(anchor.height / box.height, 0.05) : 0.4),
  };
};
const readVars = (box) => ({
  dx: box.style.getPropertyValue('--modal-dx'), dy: box.style.getPropertyValue('--modal-dy'),
  sx: box.style.getPropertyValue('--modal-sx'), sy: box.style.getPropertyValue('--modal-sy'),
});

test('closing an open(null) modal flies into its own icon, not off the top again', (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  const boxRect = { left: 400, top: 300, width: 200, height: 150 };
  const box = createStubElement('div', { getBoundingClientRect: rectOf(boxRect) });
  const overlay = createStubElement('div', { querySelector: (sel) => (sel === '.app-modal' ? box : null) });
  const btnRect = { left: 20, top: 20, width: 40, height: 32 };
  const openBtn = createStubElement('button', { getBoundingClientRect: rectOf(btnRect) });

  const shell = wireModalShell(overlay, openBtn, null);

  // Boot auto-chooser: nothing was clicked, so the OPEN correctly falls from above.
  shell.open(null);
  assert.deepEqual(readVars(box), expectedVars(boxRect, null), 'open(null) has no gesture to grow out of');

  // The FIRST close, before any real click on the toolbar icon ever happened: it must still target that
  // icon, which is plainly on screen.
  shell.close();
  assert.deepEqual(readVars(box), expectedVars(boxRect, openBtn.getBoundingClientRect()),
    'close still has the toolbar icon to shrink into, even though open() never claimed one');
});

// The other way round: the control a window BELONGS to can be gone by the time it closes (#load-image-btn
// hides the moment an image exists), so it collapses into the CANVAS instead (user report).
test('a window whose opener is gone by close time collapses into the canvas', (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  const boxRect = { left: 400, top: 300, width: 200, height: 150 };
  const box = createStubElement('div', { getBoundingClientRect: rectOf(boxRect) });
  const overlay = createStubElement('div', { querySelector: (sel) => (sel === '.app-modal' ? box : null) });
  // The opener measures 0x0 — exactly what a display:none control reports.
  const goneRect = { left: 0, top: 0, width: 0, height: 0 };
  const openBtn = createStubElement('button', { getBoundingClientRect: rectOf(goneRect) });
  // The canvas viewport the shell looks up by id.
  const viewport = createStubElement('div', {
    getBoundingClientRect: rectOf({ left: 100, top: 100, width: 600, height: 400 }),
  });
  const prevGet = globalThis.document.getElementById;
  globalThis.document.getElementById = (id) => (id === 'canvas-viewport' ? viewport : prevGet?.call(globalThis.document, id));
  t.after(() => { globalThis.document.getElementById = prevGet; });

  const shell = wireModalShell(overlay, openBtn, null);
  shell.open(openBtn);
  shell.close();

  // A 40px box centred on the viewport (100+600/2, 100+400/2) = (400, 300).
  const home = { left: 380, top: 280, right: 420, bottom: 320, width: 40, height: 40 };
  assert.deepEqual(readVars(box), expectedVars(boxRect, home),
    'the exit lands on the canvas, not above the top edge');
  // …and emphatically NOT the fall-from-above shape.
  assert.notDeepEqual(readVars(box), expectedVars(boxRect, null));
});
