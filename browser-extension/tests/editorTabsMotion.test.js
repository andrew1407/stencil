// The two editor-mode lists are rebuilt wholesale on every keystroke, so what leaves and arrives
// is decided by the tab ids of the two renders. Mechanics: tests/motion.test.js.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { editorRow, matchEditors, matchSourceTabs, sourceTabChoices } from '../src/lib/menu/editorTabs.js';
import { createFilterTransition } from '../src/lib/motion.js';
import { makeList, renderKeys } from './helpers/listDom.js';

const EDITORS = [
  editorRow({ id: 1, url: 'http://localhost:8080/', title: 'Sunset' }, { projectName: 'Sunset' }),
  editorRow({ id: 2, url: 'http://localhost:8080/', title: 'Harbour' }, { projectName: 'Harbour' }),
  editorRow({ id: 3, url: 'http://localhost:8080/', title: 'Sunrise' }, { projectName: 'Sunrise' }),
];
const editorKeys = (q) => matchEditors(EDITORS, q).map((r) => String(r.tabId));

test('the editor search leaves and re-enters rows symmetrically', () => {
  const list = makeList();
  const tr = createFilterTransition({ list, reduced: () => false });

  renderKeys(list, tr, editorKeys(''));
  assert.deepEqual(renderKeys(list, tr, editorKeys('sun')), { entered: [], left: ['2'] });
  assert.equal(tr.ghostCount, 1, 'the excluded editor plays out');
  assert.deepEqual(renderKeys(list, tr, editorKeys('sunset')), { entered: [], left: ['3'] });
  assert.deepEqual(renderKeys(list, tr, editorKeys('')), { entered: ['2', '3'], left: [] },
    'clearing the box brings both back as arrivals');
  assert.deepEqual(list.keys(), ['1', '2', '3']);
  assert.equal(tr.ghostCount, 0, 'a burst of typing leaks no half-faded rows');
});

test('the source-page filter animates the same way, and reduced motion just lands', () => {
  const choices = sourceTabChoices([
    { id: 10, url: 'https://example.com/a', title: 'A' },
    { id: 11, url: 'https://other.org/b', title: 'B' },
  ], {});
  const keys = (q) => matchSourceTabs(choices, q).map((c) => String(c.tabId));

  const list = makeList();
  const tr = createFilterTransition({ list, reduced: () => false });
  renderKeys(list, tr, keys(''));
  assert.deepEqual(renderKeys(list, tr, keys('example')), { entered: [], left: ['11'] });

  const still = makeList();
  const quiet = createFilterTransition({ list: still, reduced: () => true });
  renderKeys(still, quiet, keys(''));
  renderKeys(still, quiet, keys('example'));
  assert.deepEqual(still.keys(), ['10'], 'reduced motion: the right set, with no animation');
  assert.equal(quiet.ghostCount, 0);
});

test('editor mode wraps both rebuilds and keys its rows by tab id', () => {
  const js = ['editor/list.js', 'list/sourceTabsList.js'].map((f) => readFileSync(new URL(`../src/popup/${f}`, import.meta.url), 'utf8')).join('\n');
  assert.match(js, /edTransition\.begin\(\);\s*\n\s*listEl\.textContent = '';/);
  assert.match(js, /srcTransition\.begin\(\);\s*\n\s*listedEl\.textContent = '';/);
  assert.match(js, /edTransition\.end\(\)/);
  assert.match(js, /srcTransition\.end\(\)/);
  assert.equal((js.match(/li\.dataset\.key = String\((row|c)\.tabId\)/g) || []).length, 2,
    'both row builders carry the key the transition diffs by');
});
