// The CLI-mode gate (js/console/shell/gate.js): one switch, one refusal, and the settings it covers.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { installDom } from '../../helpers/dom.js';
const events = [];
const doc = installDom({}, { window: { addEventListener: (t, fn) => events.push([t, fn]), removeEventListener() {}, dispatchEvent(e) { for (const [t, fn] of events) if (t === e.type) fn(e); return true; } } });
const { cliModeOn, setCliMode, cliModeGate, GATED_SETTINGS } = await import('../../../js/console/shell/gate.js');
const { EVENTS } = await import('../../../js/eventBus/appBus.js');

test('off by default, the gate lets everything through; on, it throws one message and announces the change', () => {
  const seen = [];
  globalThis.window.addEventListener(EVENTS.cliModeChanged, (e) => seen.push(e.detail));
  assert.equal(cliModeOn(), false);
  assert.doesNotThrow(() => cliModeGate('a window'));
  assert.equal(setCliMode(true), true);
  assert.throws(() => cliModeGate('a window'), /stencil: CLI mode is on — a window waits until \/clioff/);
  assert.equal(setCliMode(true), true, 'a repeat changes nothing');
  assert.equal(setCliMode(false), false);
  assert.doesNotThrow(() => cliModeGate('a window'));
  assert.ok(GATED_SETTINGS.includes('motionSpeed') && GATED_SETTINGS.includes('consoleSpeed') && !GATED_SETTINGS.includes('lineColor'));
  assert.deepEqual(seen, [true, false], 'announced on each change only');
  assert.ok(Object.isFrozen(GATED_SETTINGS));
  void doc;
});
