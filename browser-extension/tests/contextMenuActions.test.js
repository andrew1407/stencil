// resolveContextAction in src/lib/contextMenu.js: the {action, src, …} each menu id resolves to,
// and the Pin/Unpin label the probe's record decides.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { MENU, resolveContextAction, pinItemTitle } from '../src/lib/menu/contextMenu.js';

test('resolveContextAction: background/link items mirror the <img> open/crop actions', () => {
  assert.deepEqual(resolveContextAction({ menuItemId: MENU.BG_OPEN, srcUrl: 'https://x/bg.jpg' }),
    { action: 'open', src: 'https://x/bg.jpg', incognito: false });
  assert.deepEqual(resolveContextAction({ menuItemId: MENU.BG_CROP, srcUrl: 'https://x/bg.jpg' }),
    { action: 'crop', src: 'https://x/bg.jpg' });
  assert.equal(resolveContextAction({ menuItemId: MENU.BG_OPEN_INCOGNITO, srcUrl: 'a' }).incognito, true);
  assert.equal(resolveContextAction({ menuItemId: MENU.BG_OPEN_RESUME, srcUrl: 'a' }).open, 'resume');
  // background path resolves from the recorded URL (no native srcUrl on a plain div)
  assert.equal(resolveContextAction({ menuItemId: MENU.BG_OPEN_MODAL }, 'https://x/bg.jpg').action, 'open-modal');
  // main target → no `target` key
  assert.ok(!('target' in resolveContextAction({ menuItemId: MENU.BG_OPEN, srcUrl: 'a' })));
});

test('resolveContextAction: desktop items resolve to a plain {action:"desktop", src}', () => {
  for (const id of [MENU.DESKTOP, MENU.FRAME_DESKTOP, MENU.BG_DESKTOP])
    assert.deepEqual(resolveContextAction({ menuItemId: id, srcUrl: 'https://x/i.jpg' }),
      { action: 'desktop', src: 'https://x/i.jpg' });   // no incognito / open / target keys
});

test('resolveContextAction: video-frame items mirror the image open/crop actions', () => {
  assert.deepEqual(resolveContextAction({ menuItemId: MENU.FRAME_OPEN, srcUrl: 'data:image/jpeg;base64,zz' }),
    { action: 'open', src: 'data:image/jpeg;base64,zz', incognito: false });
  assert.deepEqual(resolveContextAction({ menuItemId: MENU.FRAME_CROP, srcUrl: 'data:image/jpeg;base64,zz' }),
    { action: 'crop', src: 'data:image/jpeg;base64,zz' });
  assert.equal(resolveContextAction({ menuItemId: MENU.FRAME_MODAL_INCOGNITO, srcUrl: 'a' }).incognito, true);
  // frame items act on the current frame ('main' target), so carry no target key
  assert.ok(!('target' in resolveContextAction({ menuItemId: MENU.FRAME_OPEN, srcUrl: 'a' })));
});

test('resolveContextAction: preview items target the poster (open-tab / open / crop)', () => {
  assert.deepEqual(resolveContextAction({ menuItemId: MENU.PREVIEW_TAB, srcUrl: 'https://x/p.jpg' }),
    { action: 'open-tab', src: 'https://x/p.jpg', target: 'preview' });
  assert.deepEqual(resolveContextAction({ menuItemId: MENU.PREVIEW_OPEN, srcUrl: 'https://x/p.jpg' }),
    { action: 'open', src: 'https://x/p.jpg', incognito: false, target: 'preview' });
  assert.deepEqual(resolveContextAction({ menuItemId: MENU.PREVIEW_OPEN_INCOGNITO, srcUrl: 'https://x/p.jpg' }),
    { action: 'open', src: 'https://x/p.jpg', incognito: true, target: 'preview' });
  assert.deepEqual(resolveContextAction({ menuItemId: MENU.PREVIEW_CROP, srcUrl: 'https://x/p.jpg' }),
    { action: 'crop', src: 'https://x/p.jpg', target: 'preview' });
  // No poster recorded → no URL → null (the submenu is a no-op on a non-video spot).
  assert.equal(resolveContextAction({ menuItemId: MENU.PREVIEW_OPEN }, null), null);
});

test('resolveContextAction: main items never carry a target key', () => {
  for (const id of [MENU.OPEN, MENU.OPEN_INCOGNITO, MENU.OPEN_MODAL, MENU.CROP])
    assert.ok(!('target' in resolveContextAction({ menuItemId: id, srcUrl: 'a' })));
});

test('resolveContextAction: <img> open uses info.srcUrl', () => {
  const act = resolveContextAction({ menuItemId: MENU.OPEN, srcUrl: 'https://x/a.png' });
  assert.deepEqual(act, { action: 'open', src: 'https://x/a.png', incognito: false });
});

test('resolveContextAction: background open uses the recorded URL (no srcUrl)', () => {
  const act = resolveContextAction({ menuItemId: MENU.OPEN }, 'https://x/bg.jpg');
  assert.deepEqual(act, { action: 'open', src: 'https://x/bg.jpg', incognito: false });
});

test('resolveContextAction: srcUrl wins over the recorded background URL', () => {
  const act = resolveContextAction({ menuItemId: MENU.OPEN, srcUrl: 'https://x/real.png' }, 'https://x/bg.jpg');
  assert.equal(act.src, 'https://x/real.png');
});

test('resolveContextAction: incognito + crop variants', () => {
  assert.equal(resolveContextAction({ menuItemId: MENU.OPEN_INCOGNITO, srcUrl: 'a' }).incognito, true);
  assert.equal(resolveContextAction({ menuItemId: MENU.OPEN_INCOGNITO }, 'b').incognito, true);
  assert.deepEqual(resolveContextAction({ menuItemId: MENU.CROP, srcUrl: 'a' }), { action: 'crop', src: 'a' });
  assert.deepEqual(resolveContextAction({ menuItemId: MENU.CROP }, 'b'), { action: 'crop', src: 'b' });
});

test('resolveContextAction: resume variant carries open:resume; plain open does not', () => {
  assert.deepEqual(resolveContextAction({ menuItemId: MENU.OPEN_RESUME, srcUrl: 'https://x/a.png' }),
    { action: 'open', src: 'https://x/a.png', incognito: false, open: 'resume' });
  // background-image path (no srcUrl) resolves from the recorded URL and still carries it
  assert.equal(resolveContextAction({ menuItemId: MENU.OPEN_RESUME }, 'https://x/bg.jpg').open, 'resume');
  // a plain open never grows an `open` key (keeps the editor's default import path)
  assert.ok(!('open' in resolveContextAction({ menuItemId: MENU.OPEN, srcUrl: 'a' })));
});

test('resolveContextAction: in-page modal variants carry the open-modal action', () => {
  assert.deepEqual(resolveContextAction({ menuItemId: MENU.OPEN_MODAL, srcUrl: 'a' }),
    { action: 'open-modal', src: 'a', incognito: false });
  assert.deepEqual(resolveContextAction({ menuItemId: MENU.OPEN_MODAL_INCOGNITO, srcUrl: 'a' }),
    { action: 'open-modal', src: 'a', incognito: true });
  // background-image path (no srcUrl) resolves from the recorded URL too
  assert.equal(resolveContextAction({ menuItemId: MENU.OPEN_MODAL }, 'b').action, 'open-modal');
});

test('resolveContextAction: null when id is foreign or no URL is available', () => {
  assert.equal(resolveContextAction({ menuItemId: 'someone-elses-menu', srcUrl: 'a' }), null);
  assert.equal(resolveContextAction({ menuItemId: MENU.OPEN }, null), null);   // no image under cursor
  assert.equal(resolveContextAction({ menuItemId: MENU.CROP }), null);
});

test('pinItemTitle: Pin when unpinned, Unpin when pinned, image vs video', () => {
  assert.equal(pinItemTitle(false, 'image'), '📌 Pin image');
  assert.equal(pinItemTitle(true, 'image'), '📌 Unpin image');
  assert.equal(pinItemTitle(false, 'video'), '📌 Pin video');
  assert.equal(pinItemTitle(true, 'video'), '📌 Unpin video');
  assert.equal(pinItemTitle(true), '📌 Unpin image');   // defaults to image
});
