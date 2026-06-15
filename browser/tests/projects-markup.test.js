import { test } from 'node:test';
import assert from 'node:assert';

// layout() transitively imports every ui component, including the projects modal.
import { layout } from '../js/ui/layout.js';

const markup = layout();
const count = needle => markup.split(needle).length - 1;

const NEW_IDS = [
  'projects-btn', 'incognito-toggle', 'projects-modal-overlay', 'projects-close',
  'projects-list', 'projects-new-editor', 'projects-clear-all', 'projects-search',
];

test('each new projects id appears exactly once', () => {
  for (const id of NEW_IDS) {
    assert.strictEqual(count(`id="${id}"`), 1, `id="${id}" should appear exactly once`);
  }
});

test('#projects-list is empty/comment-only in static markup', () => {
  assert.ok(
    /<div class="settings-body" id="projects-list"><!-- filled by JS --><\/div>/.test(markup),
    'empty comment-only #projects-list present'
  );
});

test('no backticks or template interpolation leaked into markup', () => {
  assert.strictEqual(count('`'), 0, 'no backticks in markup');
  assert.strictEqual(count('${'), 0, 'no ${ in markup');
});

test('no runtime-only project row ids in static markup', () => {
  assert.strictEqual(count('project-row'), 0, 'no project-row markup statically');
  assert.strictEqual(count('project-thumb'), 0, 'no project-thumb markup statically');
});
