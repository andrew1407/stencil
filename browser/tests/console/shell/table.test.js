// The command table (js/console/shell/table.js) over config/console/commands.json: a word
// resolves by name or alias in any casing, a CLI-mode row only while the mode is on.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { COMMANDS, SECTIONS, CLI_MODE, commandOf, completions } from '../../../js/console/shell/table.js';

test('a word resolves by name or alias, in any casing', () => {
  assert.equal(commandOf('UPLOAD').name, 'upload');
  assert.equal(commandOf('load').name, 'upload');
  assert.equal(commandOf('rot').name, 'rotate');
  assert.equal(commandOf('?').name, 'help');
  assert.equal(commandOf('nope'), null);
  assert.equal(commandOf(''), null);
});

test('a CLI-mode row answers only while the mode is on, and the table knows its word', () => {
  assert.equal(commandOf('clioff'), null);
  assert.equal(commandOf('clioff', { cliMode: true }).name, 'clioff');
  assert.equal(commandOf('ls', { cliMode: true }).cliMode, true);
  assert.equal(CLI_MODE.typedWord, 'cliclicli');
  assert.equal(CLI_MODE.offCommand, 'clioff');
  assert.ok(CLI_MODE.holdMs >= 1000);
});

test('every browser row sits in a known section, names are unique, and Tab completes by prefix', () => {
  const names = new Set();
  for (const c of COMMANDS) {
    assert.ok(SECTIONS.includes(c.section), `${c.name}: section ${c.section}`);
    for (const n of [c.name, ...c.aliases]) { assert.equal(names.has(n), false, `${n} twice`); names.add(n); }
    assert.ok(c.surfaces.includes('browser'));
  }
  assert.deepEqual(completions('conn'), ['connect', 'connections']);
  assert.deepEqual(completions('cli'), []);
  assert.deepEqual(completions('cli', { cliMode: true }), ['clioff']);
});
