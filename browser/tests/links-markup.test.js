import { test } from 'node:test';
import assert from 'node:assert';

// layout() transitively imports every ui component, including the links modal.
// The links modal now only EDITS the current image's provenance; adding an image by
// URL moved to the unified Open dialog (openImageModal), so the old add-by-URL ids
// (links-url, links-preview*, links-load, quick-crop) are gone.
import { layout } from '../js/ui/layout.js';

const markup = layout();
const count = needle => markup.split(needle).length - 1;

const EDIT_IDS = [
  'links-btn', 'links-modal-overlay', 'links-close', 'links-name',
  'links-edit-section',
  'links-source', 'links-source-open', 'links-source-clear',
  'links-resource', 'links-resource-open', 'links-resource-clear',
  'links-foot-hint',
];

test('each links-modal id appears exactly once', () => {
  for (const id of EDIT_IDS) {
    assert.strictEqual(count(`id="${id}"`), 1, `id="${id}" should appear exactly once`);
  }
});

test('the retired add-by-URL controls are gone from the links modal', () => {
  for (const id of ['links-add-section', 'links-url', 'links-preview', 'links-load', 'links-crop-pagesize']) {
    assert.strictEqual(count(`id="${id}"`), 0, `id="${id}" should be removed`);
  }
});
