// core/pointer/clickOrDouble.js: one control, two meanings — a click waits out the double-click
// window, a second click inside it is the double, and a key press is a click at once.
import { test, mock } from 'node:test';
import assert from 'node:assert/strict';
import { clickOrDouble } from '../../../js/core/pointer/clickOrDouble.js';

test('clickOrDouble: one click waits out the window; a second inside it is the double; a key press is at once', () => {
  mock.timers.enable({ apis: ['setTimeout'] });
  try {
    const seen = [];
    const run = clickOrDouble({ single: () => seen.push('single'), double: () => seen.push('double') });
    run({ detail: 1 });
    mock.timers.tick(100);
    assert.deepEqual(seen, []);
    run({ detail: 2 });
    mock.timers.tick(1000);
    assert.deepEqual(seen, ['double']);
    run({ detail: 1 });
    mock.timers.tick(1000);
    run({ detail: 0 });
    assert.deepEqual(seen, ['double', 'single', 'single']);
  } finally { mock.timers.reset(); }
});
