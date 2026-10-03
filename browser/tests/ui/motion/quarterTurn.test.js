// The picture's quarter turn (js/ui/motion/quarterTurn.js): the start transform lays the new
// box over the old one turned back a quarter, and the flight plays on the clock it is handed
// only while motion is on. Node has no WAAPI, so the box is a stub. The extension port's cases.
import test from 'node:test';
import assert from 'node:assert';

import { setMotionPrefs } from '../../../js/ui/motion/motionPrefs.js';
import { quarterTurnStart, beginQuarterTurn } from '../../../js/ui/motion/quarterTurn.js';

const FROM = { left: 0, top: 0, width: 400, height: 200 };
const TO = { left: 150, top: 0, width: 100, height: 200 };

test('quarterTurnStart: CW starts a quarter back, scaled onto the old box and centred on it', () => {
  assert.strictEqual(quarterTurnStart(FROM, TO, 1), 'translate(0px, 0px) rotate(-90deg) scale(2)');
  assert.match(quarterTurnStart(FROM, TO, -1), /rotate\(90deg\)/);
});

test('quarterTurnStart: a degenerate box has no start', () => {
  assert.strictEqual(quarterTurnStart({ left: 0, top: 0, width: 0, height: 5 }, TO, 1), null);
});

const stubBox = () => {
  const rects = [FROM, TO];
  const box = { style: {}, flights: [], getBoundingClientRect: () => rects.shift(),
    animate: (frames, opts) => { const a = { frames, opts }; box.flights.push(a); return a; } };
  return box;
};

test('beginQuarterTurn: plays from the old box on the given clock, hooks around the flight', () => {
  setMotionPrefs({ mode: 'slide' });
  try {
    const box = stubBox(), viewport = { style: {} }, log = [];
    const turn = beginQuarterTurn(box, { ms: 360, easing: 'ease-out', viewport,
      onStart: () => log.push('start'), onEnd: () => log.push('end') });
    assert.deepStrictEqual(log, ['start']);
    turn.play(1);
    const [flight] = box.flights;
    assert.deepStrictEqual(flight.opts, { duration: 360, easing: 'ease-out' });
    assert.match(flight.frames[0].transform, /rotate\(-90deg\) scale\(2\)/);
    assert.strictEqual(viewport.style.overflow, 'hidden');
    flight.onfinish();
    assert.deepStrictEqual(log, ['start', 'end']);
    assert.strictEqual(viewport.style.overflow, '');
  } finally {
    setMotionPrefs({ mode: 'particles' });
  }
});

test('beginQuarterTurn: motion none plays nothing and arms no hook', () => {
  setMotionPrefs({ mode: 'none' });
  try {
    const box = stubBox(), log = [];
    beginQuarterTurn(box, { ms: 360, easing: 'ease-out', onStart: () => log.push('start') }).play(1);
    assert.strictEqual(box.flights.length, 0);
    assert.deepStrictEqual(log, []);
  } finally {
    setMotionPrefs({ mode: 'particles' });
  }
});
