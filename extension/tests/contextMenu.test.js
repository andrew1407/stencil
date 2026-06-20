import { test } from 'node:test';
import assert from 'node:assert/strict';
import { MENU, MENU_ITEMS, resolveContextAction, DYNAMIC_ITEMS, PREVIEW_ITEMS, PIN_ITEMS } from '../src/lib/contextMenu.js';

test('MENU_ITEMS: one explicit "Stencil" parent holds every item; Preview nests one deeper', () => {
  // A single top-level parent (id=root, title "Stencil") so the submenu reads "Stencil"
  // instead of the auto-grouped extension name. Everything else hangs off it; only the
  // video Preview group nests a second level.
  const root = MENU_ITEMS.find(i => i.id === MENU.root);
  assert.ok(root && root.title === 'Stencil' && !root.parentId);
  // Exactly one item has no parent: the root.
  assert.deepEqual(MENU_ITEMS.filter(i => !i.parentId).map(i => i.id), [MENU.root]);
  // Distinct parents used: the root and the Preview submenu parent.
  const parents = [...new Set(MENU_ITEMS.filter(i => i.parentId).map(i => i.parentId))].sort();
  assert.deepEqual(parents, [MENU.previewParent, MENU.root].sort());
  // previewParent hangs off root; its 6 actions hang off it.
  assert.equal(MENU_ITEMS.find(i => i.id === MENU.previewParent).parentId, MENU.root);
  assert.equal(MENU_ITEMS.filter(i => i.parentId === MENU.previewParent).length, 6);

  // Native items are scoped to native contexts: image actions on <img>, frame + preview
  // on <video>. None is on 'page'/'all' (only the root carries 'all'), so they never
  // appear on plain elements (that was the "shows on every element" regression).
  for (const id of [MENU.open, MENU.crop, MENU.openResume])
    assert.deepEqual(MENU_ITEMS.find(i => i.id === id).contexts, ['image']);
  for (const id of [MENU.frameOpen, MENU.frameCrop, MENU.previewParent, MENU.previewOpen])
    assert.deepEqual(MENU_ITEMS.find(i => i.id === id).contexts, ['video']);

  // The always-on native items (image actions, video current-frame actions) carry no
  // `visible` flag. The dynamically-gated groups (background + Preview) do.
  const native = MENU_ITEMS.filter(i => i.id !== MENU.root && !i.contexts.includes('all'));
  const nativeAlways = native.filter(i => !PREVIEW_ITEMS.includes(i.id));
  assert.ok(nativeAlways.every(i => !('visible' in i)));
  const ids = MENU_ITEMS.map(i => i.id);
  assert.equal(new Set(ids).size, ids.length);
});

test('MENU_ITEMS: dynamic background/link group hangs off the Stencil parent on the all-context, default-hidden', () => {
  const bg = MENU_ITEMS.filter(i => DYNAMIC_ITEMS.includes(i.id));
  // 7 image-equivalent actions (open / resume / incognito / 2× modal / crop / pin).
  assert.equal(bg.length, 7);
  assert.equal(DYNAMIC_ITEMS.length, 7);
  // Each hangs off the root parent on the 'all' context so it CAN show on a background div…
  assert.ok(bg.every(i => i.parentId === MENU.root && i.contexts.includes('all')));
  // …and every item carries its own visible:false, so the worker reveals them one by one.
  assert.ok(bg.every(i => i.visible === false));
});

test('MENU_ITEMS: video Preview submenu is default-hidden so it shows only when a poster exists', () => {
  const preview = MENU_ITEMS.filter(i => PREVIEW_ITEMS.includes(i.id));
  // The parent + its 6 action items.
  assert.equal(preview.length, 7);
  assert.equal(PREVIEW_ITEMS.length, 7);
  assert.ok(PREVIEW_ITEMS.includes(MENU.previewParent));
  // Every item starts hidden; the worker reveals the group only for a video with a
  // poster, so a posterless video never shows a dead (no-op) Preview submenu.
  assert.ok(preview.every(i => i.visible === false));
});

test('MENU_ITEMS: toolbar-icon (action) menu offers open-editor + incognito under the Stencil parent', () => {
  for (const id of [MENU.actionOpen, MENU.actionOpenIncognito]) {
    const it = MENU_ITEMS.find(i => i.id === id);
    assert.deepEqual(it.contexts, ['action']);   // only on the extension icon, never a page element
    assert.equal(it.parentId, MENU.root);
    assert.ok(!('visible' in it));                // always available
  }
});

test('MENU_ITEMS: a pin item sits in each context group (image / video / background)', () => {
  // One pin item per group: <img> + <video> on their native contexts, background on 'all'
  // (default-hidden, revealed with the rest of the dynamic group).
  assert.deepEqual(PIN_ITEMS, [MENU.pin, MENU.framePin, MENU.bgPin]);
  assert.deepEqual(MENU_ITEMS.find(i => i.id === MENU.pin).contexts, ['image']);
  assert.deepEqual(MENU_ITEMS.find(i => i.id === MENU.framePin).contexts, ['video']);
  const bgPin = MENU_ITEMS.find(i => i.id === MENU.bgPin);
  assert.ok(bgPin.contexts.includes('all') && bgPin.visible === false && DYNAMIC_ITEMS.includes(MENU.bgPin));
  // Pin items are handled directly in the SW, not via resolveContextAction.
  for (const id of PIN_ITEMS) assert.equal(resolveContextAction({ menuItemId: id, srcUrl: 'a' }), null);
});

test('resolveContextAction: background/link items mirror the <img> open/crop actions', () => {
  assert.deepEqual(resolveContextAction({ menuItemId: MENU.bgOpen, srcUrl: 'https://x/bg.jpg' }),
    { action: 'open', src: 'https://x/bg.jpg', incognito: false });
  assert.deepEqual(resolveContextAction({ menuItemId: MENU.bgCrop, srcUrl: 'https://x/bg.jpg' }),
    { action: 'crop', src: 'https://x/bg.jpg' });
  assert.equal(resolveContextAction({ menuItemId: MENU.bgOpenIncognito, srcUrl: 'a' }).incognito, true);
  assert.equal(resolveContextAction({ menuItemId: MENU.bgOpenResume, srcUrl: 'a' }).open, 'resume');
  // background path resolves from the recorded URL (no native srcUrl on a plain div)
  assert.equal(resolveContextAction({ menuItemId: MENU.bgOpenModal }, 'https://x/bg.jpg').action, 'open-modal');
  // main target → no `target` key
  assert.ok(!('target' in resolveContextAction({ menuItemId: MENU.bgOpen, srcUrl: 'a' })));
});

test('resolveContextAction: video-frame items mirror the image open/crop actions', () => {
  assert.deepEqual(resolveContextAction({ menuItemId: MENU.frameOpen, srcUrl: 'data:image/jpeg;base64,zz' }),
    { action: 'open', src: 'data:image/jpeg;base64,zz', incognito: false });
  assert.deepEqual(resolveContextAction({ menuItemId: MENU.frameCrop, srcUrl: 'data:image/jpeg;base64,zz' }),
    { action: 'crop', src: 'data:image/jpeg;base64,zz' });
  assert.equal(resolveContextAction({ menuItemId: MENU.frameModalIncognito, srcUrl: 'a' }).incognito, true);
  // frame items act on the current frame ('main' target), so carry no target key
  assert.ok(!('target' in resolveContextAction({ menuItemId: MENU.frameOpen, srcUrl: 'a' })));
});

test('resolveContextAction: preview items target the poster (open-tab / open / crop)', () => {
  assert.deepEqual(resolveContextAction({ menuItemId: MENU.previewTab, srcUrl: 'https://x/p.jpg' }),
    { action: 'open-tab', src: 'https://x/p.jpg', target: 'preview' });
  assert.deepEqual(resolveContextAction({ menuItemId: MENU.previewOpen, srcUrl: 'https://x/p.jpg' }),
    { action: 'open', src: 'https://x/p.jpg', incognito: false, target: 'preview' });
  assert.deepEqual(resolveContextAction({ menuItemId: MENU.previewOpenIncognito, srcUrl: 'https://x/p.jpg' }),
    { action: 'open', src: 'https://x/p.jpg', incognito: true, target: 'preview' });
  assert.deepEqual(resolveContextAction({ menuItemId: MENU.previewCrop, srcUrl: 'https://x/p.jpg' }),
    { action: 'crop', src: 'https://x/p.jpg', target: 'preview' });
  // No poster recorded → no URL → null (the submenu is a no-op on a non-video spot).
  assert.equal(resolveContextAction({ menuItemId: MENU.previewOpen }, null), null);
});

test('resolveContextAction: main items never carry a target key', () => {
  for (const id of [MENU.open, MENU.openIncognito, MENU.openModal, MENU.crop])
    assert.ok(!('target' in resolveContextAction({ menuItemId: id, srcUrl: 'a' })));
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

test('resolveContextAction: resume variant carries open:resume; plain open does not', () => {
  assert.deepEqual(resolveContextAction({ menuItemId: MENU.openResume, srcUrl: 'https://x/a.png' }),
    { action: 'open', src: 'https://x/a.png', incognito: false, open: 'resume' });
  // background-image path (no srcUrl) resolves from the recorded URL and still carries it
  assert.equal(resolveContextAction({ menuItemId: MENU.openResume }, 'https://x/bg.jpg').open, 'resume');
  // a plain open never grows an `open` key (keeps the editor's default import path)
  assert.ok(!('open' in resolveContextAction({ menuItemId: MENU.open, srcUrl: 'a' })));
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
