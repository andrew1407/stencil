import { test } from 'node:test';
import assert from 'node:assert';

// layout() transitively imports every ui component, including the links modal.
import { layout } from '../js/ui/layout.js';

const markup = layout();
const count = needle => markup.split(needle).length - 1;

const NEW_IDS = [
  'links-btn', 'links-modal-overlay', 'links-close', 'links-name',
  'links-edit-section', 'links-add-section',
  'links-source', 'links-source-open', 'links-source-clear',
  'links-resource', 'links-resource-open', 'links-resource-clear',
  'links-url', 'links-url-resource', 'links-preview', 'links-preview-wrap',
  'links-preview-img', 'links-preview-video', 'links-preview-hint',
  'links-foot-hint', 'links-load',
];

test('each links-modal id appears exactly once', () => {
  for (const id of NEW_IDS) {
    assert.strictEqual(count(`id="${id}"`), 1, `id="${id}" should appear exactly once`);
  }
});

test('the load button starts disabled', () => {
  assert.ok(markup.includes('id="links-load" disabled'), 'load disabled until preview');
});
