// The prompt line (js/ui/console/input.js): Enter runs and clears, Tab completes the first word,
// Up and Down recall with the line being typed coming back past the newest entry.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createStubElement } from '../../helpers/dom.js';
import { wireConsoleInput } from '../../../js/ui/console/input.js';

test('Enter runs the line, Tab completes, the arrows recall history and give the typed line back', () => {
  const input = createStubElement('input');
  const ran = [];
  const past = ['/status', '/old'];
  let cursor = past.length;
  const history = {
    up: () => { if (cursor > 0) cursor--; return past[cursor]; },
    down: () => { if (cursor < past.length) cursor++; return past[cursor] ?? ''; },
    atEnd: () => cursor >= past.length,
    get entries() { return past; },
  };
  wireConsoleInput(input, { run: (l) => ran.push(l), complete: (line) => (line === '/con' ? '/conn' : line === '/connec' ? '/connect ' : null), history,
    classify: (line) => (line.startsWith('/status') ? 'cmd' : line === '/meow' ? 'secret' : '') });
  const key = (k) => input.dispatch('keydown', { key: k, preventDefault() {} });
  input.value = '/status';
  input.dispatch('input', {});
  assert.equal(input.dataset.word, 'cmd', 'a command word wears the accent while typed');
  input.value = '/meow';
  input.dispatch('input', {});
  assert.equal(input.dataset.word, 'secret');
  input.value = '/status';
  key('Enter');
  assert.deepEqual(ran, ['/status']);
  assert.equal(input.value, '');
  assert.equal(input.dataset.word, '', 'cleared with the line');
  input.value = '/con';
  key('Tab');
  assert.equal(input.value, '/conn', 'extended to the common prefix');
  input.value = '/connec';
  key('Tab');
  assert.equal(input.value, '/connect ', 'one match: filled in with a space after');
  input.value = '/zzz';
  key('Tab');
  assert.equal(input.value, '/zzz', 'nothing to complete leaves the line');
  input.value = '/typing';
  key('ArrowUp');
  assert.equal(input.value, '/old');
  key('ArrowUp');
  assert.equal(input.value, '/status');
  key('ArrowDown');
  assert.equal(input.value, '/old');
  key('ArrowDown');
  assert.equal(input.value, '/typing', 'past the newest entry the typed line comes back');
});
