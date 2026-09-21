// The logo stage's kinematics (js/ui/stageMotion.js), stepped with fixed clocks.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import {
  revealTween, clampCentre, bounceState, bounceImpulse, bounceStep,
  chaseState, chaseStep, headingOfState, flyState, flyPunch, flyStep,
} from '../js/ui/logo/stageMotion.js';
import { STAGE } from '../js/ui/logo/stageRules.js';

const near = (a, b, eps = 1e-6) => Math.abs(a - b) < eps;

test('the reveal starts on the header mark and ends on the stage pose', () => {
  const from = { x: 24, y: 24, size: 32 }, to = { x: 500, y: 300, size: 330 };
  assert.deepEqual(revealTween(from, to, 0), from);
  assert.deepEqual(revealTween(from, to, 1), to);
  const mid = revealTween(from, to, 0.5);
  assert.ok(mid.size > 181 && mid.size < 330, 'eases out: past half way at p = 0.5');
  assert.deepEqual(revealTween(from, to, 2), to, 'clamped');
});

test('clampCentre keeps the whole mark inside', () => {
  assert.deepEqual(clampCentre(-10, 5000, 100, 800, 600), { x: 50, y: 550 });
  assert.deepEqual(clampCentre(400, 300, 100, 800, 600), { x: 400, y: 300 });
});

test('a click steps the size toward the impulse, rapidly, and it glides back', () => {
  const st = bounceState(330, 60);
  assert.equal(bounceStep(st, 1000), 330, 'at rest nothing moves');
  bounceImpulse(st, 1000);
  assert.ok(st.to > 60 && st.to < 330, `one click is a STEP, never the whole way (${st.to})`);
  assert.ok(near(bounceStep(st, 1120), 249), 'the click lands 30% of the way down, in 120ms');
  assert.equal(st.phase, 'recover');
  // 30% of the range, at the rate recoverMs sets for the whole of it: 900 * 0.3 = 270ms.
  assert.ok(near(bounceStep(st, 1120 + 270), 330), 'and glides home over 270ms — the click is the quick half');
  assert.equal(st.phase, 'rest');
});

test('clicking faster than it comes home is what drives it to the far size', () => {
  const run = (gapMs, clicks) => {
    const st = bounceState(330, 60);
    let t = 0;
    for (let i = 0; i < clicks; i++) {
      bounceImpulse(st, t);
      for (let k = 0; k < gapMs; k += 10) { t += 10; bounceStep(st, t); }
    }
    return st.size;
  };
  assert.ok(run(600, 6) > 320, 'clicking slower than the way home leaves it at rest');
  assert.ok(run(90, 8) < 70, 'clicking fast drives it to the far size');
});

test('a chase is thrown at the cursor and drags behind — it never simply arrives', () => {
  const st = chaseState(100, 300);
  const cursor = { x: 700, y: 300 };
  let closest = Infinity, overshot = false;
  for (let i = 0; i < 200; i++) {
    chaseStep(st, cursor, 16, 80, 800, 600, STAGE.follow);
    closest = Math.min(closest, Math.abs(st.x - cursor.x));
    if (st.x > cursor.x) overshot = true;
  }
  assert.ok(closest < 40, `it gets there (closest ${closest.toFixed(1)}px)`);
  assert.ok(overshot, 'and carries past — that is the inertia');
  assert.ok(st.x >= 40 && st.x <= 760, 'and it stays inside');
});

test('a fleeing chase only answers a cursor that is near, and keeps to the window', () => {
  const far = chaseState(400, 300);
  chaseStep(far, { x: 400, y: 1200 }, 16, 80, 800, 600, STAGE.escape, true);
  assert.ok(Math.hypot(far.vx, far.vy) < 1, 'a cursor past the radius moves it barely at all');

  const near = chaseState(400, 300);
  for (let i = 0; i < 40; i++) chaseStep(near, { x: 380, y: 300 }, 16, 80, 800, 600, STAGE.escape, true);
  assert.ok(near.x > 400, 'a cursor beside it pushes it away');
  for (let i = 0; i < 400; i++) chaseStep(near, { x: 380, y: 300 }, 16, 80, 800, 600, STAGE.escape, true);
  assert.ok(near.x <= 760 && near.x >= 40 && near.y <= 560 && near.y >= 40, 'and it never leaves');
});

test('the heading is the way it travels; a mark the cursor has caught has none', () => {
  assert.equal(headingOfState({ vx: 0, vy: 0 }), null);
  assert.equal(headingOfState({ vx: 6, vy: -8 }), null, 'a crawl is not a direction — grains ring it');
  const h = headingOfState({ vx: 300, vy: -400 });
  assert.ok(Math.abs(h.x - 0.6) < 1e-9 && Math.abs(h.y + 0.8) < 1e-9, 'a unit vector');
});

test('fly bounces off the walls, keeps its cruise speed, and a punch damps back', () => {
  const st = flyState(790, 300, 0);
  flyStep(st, 100, 100, 800, 600);
  assert.ok(st.vx < 0, 'reflected off the right wall');
  assert.ok(st.x <= 750, 'and back inside');
  flyPunch(st, Math.PI / 2);
  assert.ok(near(Math.hypot(st.vx, st.vy), 900), 'punch = cruise + punch speed');
  for (let i = 0; i < 100; i++) flyStep(st, 100, 100, 800, 600);
  assert.ok(Math.hypot(st.vx, st.vy) < 270, 'damped back near cruise');
  assert.ok(Math.hypot(st.vx, st.vy) >= 260 - 1e-6, 'never below cruise');
});
