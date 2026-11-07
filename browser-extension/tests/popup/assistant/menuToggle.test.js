// A ⋯ shuts only the menu it opened: its press is never the outside press that closes the menu
// first, so the click that follows cannot reopen it. Browser twin: projects/window/projectRowMenu.js.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';

const read = (rel) => readFileSync(new URL(rel, import.meta.url), 'utf8');

test('the bubble ⋯ toggles its own menu and opens over one a right-click opened', () => {
  const s = read('../../../src/popup/assistant/msgMenu.js');
  assert.ok(s.includes('if (msgMenu.isOpen() && openedBy === btn) { openedBy = null; msgMenu.close(); return; }'));
  assert.ok(s.includes('!openedBy?.contains(e.target)) msgMenu.close();'), 'its press is not an outside press');
  assert.match(s, /e\.preventDefault\(\);\s+openedBy = null;\s+openMenuAt\(el, e\.clientX, e\.clientY\);/);
});

test('a press anywhere on the composer ⋯, its glyph included, leaves the toggle to its click', () => {
  const s = read('../../../src/popup/assistant/composerMenu.js');
  assert.ok(s.includes('!moreMenu.contains(e.target) && !moreBtn.contains(e.target)) closeMore();'));
});

test('the row ⋯ closes the menu it anchored and re-anchors one opened at a point', () => {
  const s = read('../../../src/lib/control/actionMenu.js');
  assert.ok(s.includes('if (anchorBtn === btn) return close();'));
  assert.match(s, /const openAt = \(x, y, fill\) => \{\s+close\(\);/, 'a point-opened menu clears the anchor');
});
