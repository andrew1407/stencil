import { test } from 'node:test';
import assert from 'node:assert';

// layout() transitively imports every ui component, including the blank-image modal.
import { layout } from '../js/ui/layout.js';

const markup = layout();
const count = needle => markup.split(needle).length - 1;

const NEW_IDS = [
  'idle-create-wrap', 'create-blank-btn', 'blank-image-modal-overlay', 'blank-image-close',
  'blank-image-white', 'blank-image-black', 'blank-image-color', 'blank-image-width',
  'blank-image-height', 'blank-image-create', 'projects-blank-image',
];

test('each blank-image id appears exactly once', () => {
  for (const id of NEW_IDS) {
    assert.strictEqual(count(`id="${id}"`), 1, `id="${id}" should appear exactly once`);
  }
});

test('blank-image color picker defaults to white', () => {
  assert.ok(markup.includes('id="blank-image-color" value="#ffffff"'), 'white default');
});

test('size inputs carry the 1–8192 px bounds', () => {
  assert.ok(markup.includes('id="blank-image-width" min="1" max="8192"'), 'width bounds');
  assert.ok(markup.includes('id="blank-image-height" min="1" max="8192"'), 'height bounds');
});

test('no backticks or template interpolation leaked into markup', () => {
  assert.strictEqual(count('`'), 0, 'no backticks in markup');
  assert.strictEqual(count('${'), 0, 'no ${ in markup');
});
