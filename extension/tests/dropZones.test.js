import { test } from 'node:test';
import assert from 'node:assert/strict';
import { quadrantAt } from '../src/lib/dropZones.js';

// The 4-quadrant map the on-page drop overlay uses (see lib/dropZones.js / popup drag).
test('quadrantAt maps each corner to its action', () => {
  const W = 1000, H = 800;
  assert.equal(quadrantAt(10, 10, W, H), 'here');          // top-left
  assert.equal(quadrantAt(990, 10, W, H), 'incognito');    // top-right
  assert.equal(quadrantAt(10, 790, W, H), 'newtab');       // bottom-left
  assert.equal(quadrantAt(990, 790, W, H), 'crop');        // bottom-right
});

test('quadrantAt splits on the exact midpoint (< is top/left)', () => {
  const W = 1000, H = 800;
  // Exactly on the divide counts as the far (right/bottom) half, since the test is `< half`.
  assert.equal(quadrantAt(500, 400, W, H), 'crop');
  assert.equal(quadrantAt(499, 399, W, H), 'here');
  assert.equal(quadrantAt(500, 399, W, H), 'incognito');
  assert.equal(quadrantAt(499, 400, W, H), 'newtab');
});
