// A modal opened with NO gesture behind it (open(null) — the projects modal's on-boot
// auto-chooser, see projectsModal.js) still deserves a normal CLOSE: shrinking into the
// icon that would reopen it, not falling off the top of the screen a second time.
//
// The bug: wireModalShell (js/ui/base.js) resolved the flight's origin only in open(),
// leaving `originEl` null for the rest of the modal's life once it opened with no
// gesture. close() reused that stale null, so --modal-dy kept the "fell from above"
// value even though the toolbar icon was right there on screen.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createStubElement, installDom } from './helpers/dom.js';

// A rect shaped like a real getBoundingClientRect() result (has `.bottom`, which
// setOrigin's onScreen check reads directly rather than deriving from top+height).
const rectOf = (r) => () => ({ ...r, bottom: r.top + r.height });

installDom({}, {
  // base.js and motion.js now ask the SAME gate (ui/motionPrefs.js), so reduced motion is
  // off here — setOriginVars must actually run — and the particles are switched off through
  // the motion mode below instead. The flight math under test happens in setOrigin either way.
  window: { matchMedia: () => ({ matches: false }), innerWidth: 1000, innerHeight: 800, addEventListener: () => {} },
  matchMedia: () => ({ matches: false }),
});

const { wireModalShell } = await import('../js/ui/base.js');
const { setMotionPrefs } = await import('../js/ui/motionPrefs.js');
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

  // The FIRST close, before any real click on the toolbar icon ever happened — this is
  // exactly the reported repro (a saved project restored, Projects auto-opens, the user
  // hits its own Close button). It must target the icon, which is plainly on-screen.
  shell.close();
  assert.deepEqual(readVars(box), expectedVars(boxRect, openBtn.getBoundingClientRect()),
    'close still has the toolbar icon to shrink into, even though open() never claimed one');
});
