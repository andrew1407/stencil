// A window opened under Sliding lands wearing `genie-hold` (animation: none), which stills its CSS entrance for
// good. A mode picked while it is up then closes it on its CSS flight — Transition's modalToIcon — which that
// hold would still too: the window just vanished (user report). The flight drops the hold as it takes over.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createStubElement, installDom } from '../../helpers/dom.js';

const rectOf = (r) => () => ({ ...r, bottom: r.top + r.height });
installDom({}, {
  window: { matchMedia: () => ({ matches: false }), innerWidth: 1000, innerHeight: 800, addEventListener: () => {} },
  matchMedia: () => ({ matches: false }),
});
const { wireModalShell } = await import('../../../js/ui/base.js');
const { setMotionPrefs } = await import('../../../js/ui/motion/motionPrefs.js');
const { GENIE_HOLD_CLASS } = await import('../../../js/ui/motion/genie/window.js');

test('closing under Transition a window that Sliding opened drops its hold, so modalToIcon plays', (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  const box = createStubElement('div', { getBoundingClientRect: rectOf({ left: 400, top: 300, width: 200, height: 150 }) });
  const overlay = createStubElement('div', { querySelector: (sel) => (sel === '.app-modal' ? box : null) });
  const openBtn = createStubElement('button', { getBoundingClientRect: rectOf({ left: 20, top: 20, width: 40, height: 32 }) });
  const shell = wireModalShell(overlay, openBtn, null);
  setMotionPrefs({ mode: 'transition' });
  shell.open();
  box.classList.add(GENIE_HOLD_CLASS);   // what a Sliding pour leaves on landing
  shell.close();
  assert.ok(overlay.classList.contains('modal-closing'), 'the close flight is under way');
  assert.ok(!box.classList.contains(GENIE_HOLD_CLASS), 'and nothing stills it');
});
