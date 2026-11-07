// An open chat panel's entrance is a per-mode CSS animation, so a mode picked while it was up replayed it as if it
// reopened (js/ui/chat/panel/motionSettle.js): it is settled on the change and unsettled once it closes. And a menu
// with no CSS entrance of its own grows out of its origin under Transition (motion/genie/window.js menuOpen).
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { installDustPage, boxEl, rect } from '../../helpers/dustRig.js';

const { settleOnMotionChange, MOTION_SETTLED_CLASS } = await import('../../../js/ui/chat/panel/motionSettle.js');
const { publish, EVENTS } = await import('../../../js/eventBus/appBus.js');
const { menuOpen, MENU_GROW_CLASS } = await import('../../../js/ui/motion/genie/window.js');
const { setMotionPrefs } = await import('../../../js/ui/motion/motionPrefs.js');

test('a mode change settles the open panel, never a closed or closing one, and the teardown stops it', (t) => {
  installDustPage(t, { window: new EventTarget() });   // the bus delivers on window
  const host = boxEl(rect(0, 0, 340, 600));
  const stop = settleOnMotionChange(host);
  publish(EVENTS.motionChanged);
  assert.ok(!host.classList.contains(MOTION_SETTLED_CLASS), 'closed: nothing to settle');
  host.classList.add('chat-open', 'chat-closing');
  publish(EVENTS.motionChanged);
  assert.ok(!host.classList.contains(MOTION_SETTLED_CLASS), 'closing: its exit still plays');
  host.classList.remove('chat-closing');
  publish(EVENTS.motionChanged);
  assert.ok(host.classList.contains(MOTION_SETTLED_CLASS), 'open: its entrance does not replay');
  host.classList.remove(MOTION_SETTLED_CLASS);
  stop();
  publish(EVENTS.motionChanged);
  assert.ok(!host.classList.contains(MOTION_SETTLED_CLASS), 'torn down: no longer listening');
});

test('Transition grows a menu out of the point it opened from; other modes leave it to their own flight', (t) => {
  const p = installDustPage(t);
  const menu = boxEl(rect(100, 100, 200, 160));
  setMotionPrefs({ mode: 'transition' });
  assert.equal(menuOpen(menu, { left: 280, top: 270, width: 16, height: 2 }), true);
  assert.ok(menu.classList.contains(MENU_GROW_CLASS));
  assert.equal(menu.style.transformOrigin, '188px 160px', 'the origin clamped into the menu box');
  for (const ms of p.delays()) p.fire(ms);
  assert.ok(!menu.classList.contains(MENU_GROW_CLASS) && !menu.style.transformOrigin, 'cleared once it has grown');
  setMotionPrefs({ mode: 'particles' });
  assert.equal(menuOpen(menu, { left: 280, top: 270, width: 16, height: 2 }), false);
  assert.ok(!menu.classList.contains(MENU_GROW_CLASS));
});

// A classList.remove rewrites the class attribute even for an absent token, so an observer that removed on every
// class mutation re-fired itself forever and hung the page at load (user report): it only removes what is there.
test('the close watch never rewrites the class it watches when there is nothing to drop', (t) => {
  let fire = null;
  class Observer { constructor(cb) { fire = cb; } observe() {} disconnect() {} }
  installDustPage(t, { window: new EventTarget(), MutationObserver: Observer });
  const host = boxEl(rect(0, 0, 340, 600));
  settleOnMotionChange(host);
  let removes = 0;
  const remove = host.classList.remove.bind(host.classList);
  host.classList.remove = (...c) => { removes += 1; remove(...c); };
  fire([]);
  assert.equal(removes, 0, 'closed and unsettled: no write, so no fresh mutation');
  host.classList.add(MOTION_SETTLED_CLASS);
  fire([]);
  assert.equal(removes, 1, 'settled and closed: dropped once');
  fire([]);
  assert.equal(removes, 1, '…and then left alone');
});
