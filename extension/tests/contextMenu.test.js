import { test } from 'node:test';
import assert from 'node:assert/strict';
import { MENU, MENU_ITEMS, resolveContextAction, DYNAMIC_ITEMS, PREVIEW_ITEMS } from '../src/lib/contextMenu.js';

test('MENU_ITEMS: no explicit Stencil parent — items sit at top level; Preview is the only submenu', () => {
  // We create NO "Stencil" parent: Chrome auto-groups an extension's multiple top-level
  // items under its own name, so an own parent only double-nested. The lone submenu is
  // the video Preview group.
  const withChildren = [...new Set(MENU_ITEMS.filter(i => i.parentId).map(i => i.parentId))];
  assert.deepEqual(withChildren, [MENU.previewParent]);
  // The Preview submenu nests its own 6 action items.
  const preview = MENU_ITEMS.filter(i => i.parentId === MENU.previewParent);
  assert.equal(preview.length, 6);
  assert.ok(MENU_ITEMS.some(i => i.id === MENU.previewParent && !i.parentId));

  // Native items are scoped to native contexts: image actions on <img>, frame + preview
  // on <video>. None is on 'page'/'all', so they never appear on plain elements (that
  // was the "shows on every element" regression).
  for (const id of [MENU.open, MENU.crop, MENU.openResume])
    assert.deepEqual(MENU_ITEMS.find(i => i.id === id).contexts, ['image']);
  for (const id of [MENU.frameOpen, MENU.frameCrop, MENU.previewParent, MENU.previewOpen])
    assert.deepEqual(MENU_ITEMS.find(i => i.id === id).contexts, ['video']);
  const native = MENU_ITEMS.filter(i => !i.contexts.includes('all'));
  assert.ok(native.every(i => !i.contexts.includes('page') && !i.contexts.includes('all')));

  // The always-on native items (image actions, video current-frame actions) carry no
  // `visible` flag. The two dynamically-gated groups DO: the background group and the
  // video Preview submenu (revealed only when the probed video has a poster).
  const nativeAlways = native.filter(i => !PREVIEW_ITEMS.includes(i.id));
  assert.ok(nativeAlways.every(i => !('visible' in i)));
  const ids = MENU_ITEMS.map(i => i.id);
  assert.equal(new Set(ids).size, ids.length);
});

test('MENU_ITEMS: dynamic background/link group is top-level on the all-context and every item is default-hidden', () => {
  const bg = MENU_ITEMS.filter(i => DYNAMIC_ITEMS.includes(i.id));
  // 6 image-equivalent actions (open / resume / incognito / 2× modal / crop).
  assert.equal(bg.length, 6);
  assert.equal(DYNAMIC_ITEMS.length, 6);
  // Each sits at top level (no parent) on the 'all' context so it CAN show on a background div…
  assert.ok(bg.every(i => !i.parentId && i.contexts.includes('all')));
  // …but with no parent to inherit hidden state from, EVERY item must carry its own
  // visible:false, so the worker reveals them one by one.
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
