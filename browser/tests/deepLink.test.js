import { test } from 'node:test';
import assert from 'node:assert';

import { OPEN_PARAM, readOpenProjectId, buildOpenProjectUrl } from '../js/core/deepLink.js';

test('readOpenProjectId returns the project id from the open param', () => {
  assert.strictEqual(readOpenProjectId('?open=p_1'), 'p_1');
  assert.strictEqual(readOpenProjectId('?open=p_1&x=2'), 'p_1');
  assert.strictEqual(readOpenProjectId('?x=2&open=abc'), 'abc');
});

test('readOpenProjectId decodes URL-encoded ids', () => {
  assert.strictEqual(readOpenProjectId('?open=p%20a'), 'p a');
});

test('readOpenProjectId returns null when absent or empty', () => {
  assert.strictEqual(readOpenProjectId(''), null);
  assert.strictEqual(readOpenProjectId('?x=2'), null);
  assert.strictEqual(readOpenProjectId('?open='), null);
  assert.strictEqual(readOpenProjectId(), null);
});

test('buildOpenProjectUrl appends the encoded open param', () => {
  assert.strictEqual(
    buildOpenProjectUrl('https://app.example/editor', 'p_1'),
    'https://app.example/editor?open=p_1'
  );
  assert.strictEqual(
    buildOpenProjectUrl('https://app.example/editor', 'p a'),
    'https://app.example/editor?open=p%20a'
  );
});

test('buildOpenProjectUrl output round-trips through readOpenProjectId', () => {
  for (const id of ['p_1', 'abc-123', 'weird id&=?']) {
    const url = buildOpenProjectUrl('https://x/y', id);
    const search = url.slice(url.indexOf('?'));
    assert.strictEqual(readOpenProjectId(search), id, `round-trip ${id}`);
  }
});

test('OPEN_PARAM is the documented param name', () => {
  assert.strictEqual(OPEN_PARAM, 'open');
});
