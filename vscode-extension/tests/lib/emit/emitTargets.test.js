// What `--script-emit` can be asked for, and where the emitted file lands: the CLI reads the
// target off the output's extension, so the pick is the language choice.
import test from 'node:test';
import assert from 'node:assert/strict';

import { EMIT_TARGETS, emitTarget, pickEmitTarget } from '../../../src/lib/emit/targets.js';

test('the four suffixes are the ones the CLI accepts, python first', () => {
  assert.deepEqual(EMIT_TARGETS.map((t) => t.label), ['.pystc', '.py', '.stcjs', '.js']);
  for (const target of EMIT_TARGETS) assert.match(target.description, /python|javascript/);
  assert.ok(Object.isFrozen(EMIT_TARGETS));
});

test('the emitted file lands beside the script, under its own stem', () => {
  assert.equal(emitTarget('/w/shots.stc', '.pystc'), '/w/shots.pystc');
  assert.equal(emitTarget('/w/my shots.stc', '.js'), '/w/my shots.js');
  // Only the trailing .stc is the stem's end — a dotted name keeps the rest of its name.
  assert.equal(emitTarget('/w/v1.2.stc', '.py'), '/w/v1.2.py');
  assert.equal(emitTarget('/w/SHOTS.STC', '.stcjs'), '/w/SHOTS.stcjs');
  assert.equal(emitTarget('/w/shots', '.pystc'), '/w/shots.pystc');
});

test('the pick hands back the label, and nothing when it is cancelled', async () => {
  const asked = [];
  const vscode = (picked) => ({ window: { showQuickPick: (items, options) => {
    asked.push({ items, options });
    return Promise.resolve(picked);
  } } });
  assert.equal(await pickEmitTarget(vscode({ label: '.stcjs' })), '.stcjs');
  assert.equal(await pickEmitTarget(vscode(undefined)), null);
  assert.equal(asked[0].items, EMIT_TARGETS, 'the offered list is the table itself');
  assert.match(asked[0].options.title, /^Stencil: /);
});
