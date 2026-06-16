import { test } from 'node:test';
import assert from 'node:assert/strict';
import { MENU, MENU_ITEMS, resolveContextAction } from '../src/lib/contextMenu.js';

test('MENU_ITEMS: one parent + five actions, all always-visible on the all context', () => {
  const parents = MENU_ITEMS.filter(i => !i.parentId);
  assert.deepEqual(parents.map(p => p.id), [MENU.parent]);
  const children = MENU_ITEMS.filter(i => i.parentId);
  assert.equal(children.length, 5);
  assert.ok(children.every(c => c.parentId === MENU.parent));
  // every item is on 'all' and none is hidden (no visibility toggling — see module doc)
  assert.ok(MENU_ITEMS.every(i => i.contexts.includes('all')));
  assert.ok(MENU_ITEMS.every(i => !('visible' in i)));
  // ids are unique
  const ids = MENU_ITEMS.map(i => i.id);
  assert.equal(new Set(ids).size, ids.length);
});

test('resolveContextAction: <img> open uses info.srcUrl', () => {
  const act = resolveContextAction({ menuItemId: MENU.open, srcUrl: 'https://x/a.png' });
  assert.deepEqual(act, { action: 'open', src: 'https://x/a.png', incognito: false });
});

test('resolveContextAction: background open uses the recorded URL (no srcUrl)', () => {
  const act = resolveContextAction({ menuItemId: MENU.open }, 'https://x/bg.jpg');
  assert.deepEqual(act, { action: 'open', src: 'https://x/bg.jpg', incognito: false });
});

test('resolveContextAction: srcUrl wins over the recorded background URL', () => {
  const act = resolveContextAction({ menuItemId: MENU.open, srcUrl: 'https://x/real.png' }, 'https://x/bg.jpg');
  assert.equal(act.src, 'https://x/real.png');
});

test('resolveContextAction: incognito + crop variants', () => {
  assert.equal(resolveContextAction({ menuItemId: MENU.openIncognito, srcUrl: 'a' }).incognito, true);
  assert.equal(resolveContextAction({ menuItemId: MENU.openIncognito }, 'b').incognito, true);
  assert.deepEqual(resolveContextAction({ menuItemId: MENU.crop, srcUrl: 'a' }), { action: 'crop', src: 'a' });
  assert.deepEqual(resolveContextAction({ menuItemId: MENU.crop }, 'b'), { action: 'crop', src: 'b' });
});

test('resolveContextAction: in-page modal variants carry the open-modal action', () => {
  assert.deepEqual(resolveContextAction({ menuItemId: MENU.openModal, srcUrl: 'a' }),
    { action: 'open-modal', src: 'a', incognito: false });
  assert.deepEqual(resolveContextAction({ menuItemId: MENU.openModalIncognito, srcUrl: 'a' }),
    { action: 'open-modal', src: 'a', incognito: true });
  // background-image path (no srcUrl) resolves from the recorded URL too
  assert.equal(resolveContextAction({ menuItemId: MENU.openModal }, 'b').action, 'open-modal');
});

test('resolveContextAction: null when id is foreign or no URL is available', () => {
  assert.equal(resolveContextAction({ menuItemId: 'someone-elses-menu', srcUrl: 'a' }), null);
  assert.equal(resolveContextAction({ menuItemId: MENU.open }, null), null);   // no image under cursor
  assert.equal(resolveContextAction({ menuItemId: MENU.crop }), null);
});
