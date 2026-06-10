import { test } from 'node:test';
import assert from 'node:assert';

// layout() transitively imports every ui component, including the blank-image modal.
import { layout } from '../js/ui/layout.js';

const markup = layout();
const count = needle => markup.split(needle).length - 1;

const NEW_IDS = [
  'idleCreateWrap', 'createBlankBtn', 'blankImageModalOverlay', 'blankImageClose',
  'blankImageWhite', 'blankImageBlack', 'blankImageColor', 'blankImageWidth',
  'blankImageHeight', 'blankImageCreate', 'projectsBlankImage',
];

test('each blank-image id appears exactly once', () => {
  for (const id of NEW_IDS) {
    assert.strictEqual(count(`id="${id}"`), 1, `id="${id}" should appear exactly once`);
  }
});

test('blank-image color picker defaults to white', () => {
  assert.ok(markup.includes('id="blankImageColor" value="#ffffff"'), 'white default');
});

test('size inputs carry the 1–8192 px bounds', () => {
  assert.ok(markup.includes('id="blankImageWidth" min="1" max="8192"'), 'width bounds');
  assert.ok(markup.includes('id="blankImageHeight" min="1" max="8192"'), 'height bounds');
});

test('no backticks or template interpolation leaked into markup', () => {
  assert.strictEqual(count('`'), 0, 'no backticks in markup');
  assert.strictEqual(count('${'), 0, 'no ${ in markup');
});
