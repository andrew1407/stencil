import { test } from 'node:test';
import assert from 'node:assert/strict';
import {
  MENU, MENU_ITEMS, resolveContextAction, menuVisibilityFor, visibleMenu,
  DYNAMIC_ITEMS, PREVIEW_ITEMS, PIN_ITEMS, STATIC_DESKTOP_ITEMS, pinItemTitle,
} from '../src/lib/contextMenu.js';

test('MENU_ITEMS: a "Stencil" parent per group holds every item; Preview nests one deeper', () => {
  // Two top-level parents, both titled "Stencil" (so the submenu reads "Stencil" rather
  // than the auto-grouped extension name): the STATIC one for the native image/video/
  // action contexts, and the DYNAMIC one for the probe-revealed background group. They
  // are mutually exclusive in practice — see the "(empty)" tests below for why the split
  // exists.
  const root = MENU_ITEMS.find(i => i.id === MENU.ROOT);
  assert.ok(root && root.title === 'Stencil' && !root.parentId);
  const bgRoot = MENU_ITEMS.find(i => i.id === MENU.BG_ROOT);
  assert.ok(bgRoot && bgRoot.title === 'Stencil' && !bgRoot.parentId);
  assert.deepEqual(MENU_ITEMS.filter(i => !i.parentId).map(i => i.id), [MENU.ROOT, MENU.BG_ROOT]);
  // Distinct parents used: the two roots, the three "Open in editor ▸" group parents, and
  // the Preview submenu parent.
  const parents = [...new Set(MENU_ITEMS.filter(i => i.parentId).map(i => i.parentId))].sort();
  assert.deepEqual(parents,
    [MENU.OPEN_PARENT, MENU.FRAME_OPEN_PARENT, MENU.BG_OPEN_PARENT, MENU.PREVIEW_PARENT, MENU.ROOT, MENU.BG_ROOT].sort());
  // previewParent hangs off root; its 6 actions hang off it.
  assert.equal(MENU_ITEMS.find(i => i.id === MENU.PREVIEW_PARENT).parentId, MENU.ROOT);
  assert.equal(MENU_ITEMS.filter(i => i.parentId === MENU.PREVIEW_PARENT).length, 6);
  // The image open variants nest under the "Open in editor ▸" parent (which is on root).
  assert.equal(MENU_ITEMS.find(i => i.id === MENU.OPEN_PARENT).parentId, MENU.ROOT);
  for (const id of [MENU.OPEN, MENU.OPEN_RESUME, MENU.OPEN_INCOGNITO, MENU.OPEN_MODAL, MENU.OPEN_MODAL_INCOGNITO])
    assert.equal(MENU_ITEMS.find(i => i.id === id).parentId, MENU.OPEN_PARENT);
  // Crop + Pin stay at the top level (single actions, not grouped).
  assert.equal(MENU_ITEMS.find(i => i.id === MENU.CROP).parentId, MENU.ROOT);
  assert.equal(MENU_ITEMS.find(i => i.id === MENU.PIN).parentId, MENU.ROOT);

  // Native items are scoped to native contexts: image actions on <img>, frame + preview
  // on <video>. None is on 'page'/'all' (only the root carries 'all'), so they never
  // appear on plain elements (that was the "shows on every element" regression).
  for (const id of [MENU.OPEN, MENU.CROP, MENU.OPEN_RESUME])
    assert.deepEqual(MENU_ITEMS.find(i => i.id === id).contexts, ['image']);
  for (const id of [MENU.FRAME_OPEN, MENU.FRAME_CROP, MENU.PREVIEW_PARENT, MENU.PREVIEW_OPEN])
    assert.deepEqual(MENU_ITEMS.find(i => i.id === id).contexts, ['video']);

  // The always-on native items (image actions, video current-frame actions) carry no
  // `visible` flag. The dynamically-gated groups (background, Preview, and the desktop-app
  // hand-off — gated on a configured scheme) do.
  const native = MENU_ITEMS.filter(i => i.id !== MENU.ROOT && i.id !== MENU.BG_ROOT && !i.contexts.includes('all'));
  const nativeAlways = native.filter(i => !PREVIEW_ITEMS.includes(i.id) && !STATIC_DESKTOP_ITEMS.includes(i.id));
  assert.ok(nativeAlways.every(i => !('visible' in i)));
  // The desktop-app items start hidden; the worker reveals them only when a scheme is set.
  const desktop = MENU_ITEMS.filter(i => STATIC_DESKTOP_ITEMS.includes(i.id));
  assert.equal(desktop.length, STATIC_DESKTOP_ITEMS.length);
  assert.ok(desktop.every(i => i.visible === false && i.parentId === MENU.ROOT));
  const ids = MENU_ITEMS.map(i => i.id);
  assert.equal(new Set(ids).size, ids.length);
});

test('MENU_ITEMS: dynamic background/link group hangs off its OWN root, default-hidden', () => {
  const bg = MENU_ITEMS.filter(i => DYNAMIC_ITEMS.includes(i.id));
  // Its own root + the "Open in editor ▸" parent + 5 nested open variants + crop + pin = 9.
  assert.equal(bg.length, 9);
  assert.equal(DYNAMIC_ITEMS.length, 9);
  // The group's ROOT is part of the group — revealed and hidden with its children, which
  // is what makes an empty "Stencil ▸" submenu impossible.
  assert.ok(DYNAMIC_ITEMS.includes(MENU.BG_ROOT));
  // Each hangs off that root (or the bg "Open" parent), on 'all', so the group CAN show
  // on a background div…
  assert.ok(bg.every(i => (!i.parentId && i.id === MENU.BG_ROOT)
    || i.parentId === MENU.BG_ROOT || i.parentId === MENU.BG_OPEN_PARENT));
  assert.ok(bg.every(i => i.contexts.includes('all')));
  // …and every item carries its own visible:false, so the worker reveals them together.
  assert.ok(bg.every(i => i.visible === false));
  // Nothing from the dynamic group is parented to the STATIC root: a background
  // right-click must not be able to light up the static "Stencil" entry.
  assert.equal(MENU_ITEMS.filter(i => i.parentId === MENU.ROOT && i.contexts.includes('all')).length, 0);
});

// ── The "(empty)" bug: a parent Chrome draws with nothing under it ──
// Chrome decides a PARENT's visibility from its own `contexts` alone — never from
// whether any child ended up visible. The old single contexts:['all'] root therefore
// rendered on every right-click, and on a plain/background element (native children
// don't apply, dynamic children still hidden) it painted as "Stencil ▸ (empty)".

test('the static root declares only the contexts it can serve, so Chrome hides it elsewhere', () => {
  const root = MENU_ITEMS.find(i => i.id === MENU.ROOT);
  assert.deepEqual(root.contexts, ['action', 'image', 'video']);
  assert.ok(!root.contexts.includes('all'), 'contexts:[all] is what drew the empty submenu');
  // Every child of the static root serves one of those contexts.
  const kids = MENU_ITEMS.filter(i => i.parentId === MENU.ROOT);
  assert.ok(kids.length > 0);
  assert.ok(kids.every(i => i.contexts.every(c => root.contexts.includes(c))));
});

test('a right-click with nothing to offer shows NO Stencil entry (never an empty submenu)', () => {
  // Plain page element, nothing probed → not one item, so there is no parent to draw.
  assert.deepEqual(visibleMenu('page'), []);
  assert.deepEqual(visibleMenu('selection'), []);
  assert.deepEqual(visibleMenu('link'), []);
});

test('a probed background reveals its own rooted group', () => {
  const shown = visibleMenu('page', DYNAMIC_ITEMS);
  assert.ok(shown.includes(MENU.BG_ROOT), 'the "Stencil" entry appears…');
  assert.ok(shown.includes(MENU.BG_OPEN) && shown.includes(MENU.BG_CROP) && shown.includes(MENU.BG_PIN),
    '…with its actions under it');
  assert.ok(!shown.includes(MENU.ROOT), 'the static root stays hidden on a plain element');
  // The image/video items are untouched by the reveal.
  assert.ok(!shown.includes(MENU.OPEN) && !shown.includes(MENU.FRAME_OPEN));
});

test('no visible parent is ever left without visible children, in every menu state', () => {
  const byId = new Map(MENU_ITEMS.map(i => [i.id, i]));
  const parentIds = new Set(MENU_ITEMS.filter(i => i.parentId).map(i => i.parentId));
  const states = [
    ['plain element, nothing probed', 'page', []],
    ['background probed', 'page', DYNAMIC_ITEMS],
    ['background probed + desktop scheme', 'page', [...DYNAMIC_ITEMS, MENU.BG_DESKTOP]],
    ['a real <img>', 'image', []],
    ['a real <img>, desktop scheme', 'image', STATIC_DESKTOP_ITEMS],
    ['a <video> without a poster', 'video', []],
    ['a <video> with a poster', 'video', PREVIEW_ITEMS],
    ['the toolbar icon', 'action', []],
    // Belt and braces: even a stale reveal (the probe said background, the click landed
    // on an image) must not produce an empty parent.
    ['stale reveal over an image', 'image', DYNAMIC_ITEMS],
  ];
  for (const [label, context, revealed] of states) {
    const shown = new Set(visibleMenu(context, revealed));
    for (const id of shown) {
      if (!parentIds.has(id)) continue;                       // a leaf action
      const kids = MENU_ITEMS.filter(i => i.parentId === id).filter(i => shown.has(i.id));
      assert.ok(kids.length > 0, `${label}: "${byId.get(id).title}" would render EMPTY`);
    }
  }
});

test('menuVisibilityFor: what the probe found decides which group is revealed', () => {
  assert.deepEqual(menuVisibilityFor({ url: 'https://a/bg.png' }), { bg: true, preview: false });
  // An inline-SVG data URI background (the reported repro) is a URL like any other.
  assert.deepEqual(menuVisibilityFor({ url: 'data:image/svg+xml,%3Csvg/%3E' }), { bg: true, preview: false });
  // A real <img> reports imgUrl only (the native context builds the menu) → nothing revealed.
  assert.deepEqual(menuVisibilityFor({ imgUrl: 'https://a/i.png' }), { bg: false, preview: false });
  // A video: never the background group; the Preview group only with a poster.
  assert.deepEqual(menuVisibilityFor({ video: true, url: 'data:image/jpeg;base64,FRAME' }), { bg: false, preview: false });
  assert.deepEqual(menuVisibilityFor({ video: true, poster: 'https://a/p.jpg' }), { bg: false, preview: true });
  assert.deepEqual(menuVisibilityFor(null), { bg: false, preview: false });
  assert.deepEqual(menuVisibilityFor({}), { bg: false, preview: false });
});

test('MENU_ITEMS: video Preview submenu is default-hidden so it shows only when a poster exists', () => {
  const preview = MENU_ITEMS.filter(i => PREVIEW_ITEMS.includes(i.id));
  // The parent + its 6 action items.
  assert.equal(preview.length, 7);
  assert.equal(PREVIEW_ITEMS.length, 7);
  assert.ok(PREVIEW_ITEMS.includes(MENU.PREVIEW_PARENT));
  // Every item starts hidden; the worker reveals the group only for a video with a
  // poster, so a posterless video never shows a dead (no-op) Preview submenu.
  assert.ok(preview.every(i => i.visible === false));
});

test('MENU_ITEMS: toolbar-icon (action) menu offers open-editor + incognito under the Stencil parent', () => {
  for (const id of [MENU.ACTION_OPEN, MENU.ACTION_OPEN_INCOGNITO]) {
    const it = MENU_ITEMS.find(i => i.id === id);
    assert.deepEqual(it.contexts, ['action']);   // only on the extension icon, never a page element
    assert.equal(it.parentId, MENU.ROOT);
    assert.ok(!('visible' in it));                // always available
  }
});

test('MENU_ITEMS: a pin item sits in each context group (image / video / background)', () => {
  // One pin item per group: <img> + <video> on their native contexts, background on 'all'
  // (default-hidden, revealed with the rest of the dynamic group).
  assert.deepEqual(PIN_ITEMS, [MENU.PIN, MENU.FRAME_PIN, MENU.BG_PIN]);
  // The default MENU_ITEMS titles are the unpinned ('Pin …') form the SW flips at runtime.
  assert.equal(MENU_ITEMS.find(i => i.id === MENU.PIN).title, pinItemTitle(false, 'image'));
  assert.equal(MENU_ITEMS.find(i => i.id === MENU.FRAME_PIN).title, pinItemTitle(false, 'video'));
  assert.equal(MENU_ITEMS.find(i => i.id === MENU.BG_PIN).title, pinItemTitle(false, 'image'));
  assert.deepEqual(MENU_ITEMS.find(i => i.id === MENU.PIN).contexts, ['image']);
  assert.deepEqual(MENU_ITEMS.find(i => i.id === MENU.FRAME_PIN).contexts, ['video']);
  const bgPin = MENU_ITEMS.find(i => i.id === MENU.BG_PIN);
  assert.ok(bgPin.contexts.includes('all') && bgPin.visible === false && DYNAMIC_ITEMS.includes(MENU.BG_PIN));
  // Pin items are handled directly in the SW, not via resolveContextAction.
  for (const id of PIN_ITEMS) assert.equal(resolveContextAction({ menuItemId: id, srcUrl: 'a' }), null);
});

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
