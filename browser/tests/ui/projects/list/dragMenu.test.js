// The drag-time "⋯" by the projects window's title (js/ui/projects/list/dragMenu.js), over the real
// row menu: held over the ⋯ the row's own menu opens, hovering only highlights, a flyout opens on
// hover, leaving both folds it, and only a release over an item runs it, as its click would. Hover is
// read off coordinates, so every rect here is laid out by hand.
import test from 'node:test';
import assert from 'node:assert/strict';
import { installMenuDom } from '../../../helpers/menuDom.js';
import { setMotionOverride } from '../../../../js/ui/motion/motionPrefs.js';

const doc = installMenuDom();
globalThis.window = { innerWidth: 1200, innerHeight: 800, addEventListener() {}, removeEventListener() {}, dispatchEvent: () => true };
globalThis.MouseEvent = class { constructor(type, init = {}) { this.type = type; Object.assign(this, init); } };
Object.assign(globalThis, { requestAnimationFrame: () => 0, cancelAnimationFrame() {}, matchMedia: () => ({ matches: false }),
  getComputedStyle: () => ({ color: 'rgb(1, 2, 3)', backgroundColor: 'rgb(4, 5, 6)', borderTopStyle: 'none',
    borderTopWidth: '0px', borderTopColor: 'rgb(0, 0, 0)', getPropertyValue: () => '' }) });
setMotionOverride({ mode: 'none' });

const { createProjectRowMenu } = await import('../../../../js/ui/projects/window/projectRowMenu.js');
const { createDragMenu, DRAG_MENU_SLACK_PX } = await import('../../../../js/ui/projects/list/dragMenu.js');
const { SURFACE_FORMING_CLASS, SURFACE_LEAVING_CLASS } = await import('../../../../js/ui/motion.js');
const { cloudAim, near } = await import('../../../helpers/dustCloudRig.js');

const box = (left, top, width, height) => ({ left, top, width, height, right: left + width, bottom: top + height });
const at = (el, r) => { el.getBoundingClientRect = () => r; return el; };
const lists = () => doc.body.children.filter((c) => c.classList.contains('project-menu'));
const menuItems = (list) => list.children.filter((c) => c.classList.contains('project-menu-item'));

// A header like the projects window's, and a menu that knows what ran.
const mount = () => {
  doc.body.children.slice().forEach((c) => c.remove());
  doc.body.insertAdjacentHTML('beforeend', '<div class="settings-header"><h2>Projects</h2><button id="projects-close">Close</button></div>');
  const header = doc.querySelector('.settings-header');
  const ran = [];
  const items = () => [
    { icon: 'folder', label: 'Open', onClick: (rect) => ran.push(['open', !!rect]) },
    { icon: 'duplicate', label: 'Make a copy', items: [{ icon: 'image', label: 'Image only', onClick: () => ran.push('copy-image') }] },
    { icon: 'trash', label: 'Remove', danger: true, onClick: () => ran.push('remove') },
  ];
  const rows = createProjectRowMenu();
  const menu = createDragMenu({ title: header.children[0], showMenu: rows.showMenu, closeMenu: rows.closeMenu });
  // The ⋯ sits right of the title; the menu drops under it, its rows 30px apart.
  const layOut = () => {
    const [list] = lists();
    at(list, box(120, 48, 180, 100));
    menuItems(list).forEach((b, i) => at(b, box(125, 53 + 30 * i, 170, 28)));
  };
  return { header, ran, items, menu, rows, layOut };
};

const hold = (r) => {
  r.menu.begin(r.items);
  at(r.header.children[1], box(120, 20, 30, 22));
  r.menu.track(130, 30);
  r.layOut();
};

test('held over the ⋯ the row menu opens; hovering only highlights, as a hover does', () => {
  const r = mount();
  r.menu.begin(r.items);
  const [, btn, close] = r.header.children;
  assert.ok(btn.classList.contains('projects-drag-more'), 'the ⋯ forms right after the title');
  assert.equal(close.id, 'projects-close', 'and Close stays last');
  at(btn, box(120, 20, 30, 22));
  assert.equal(r.menu.track(600, 400), false, 'away from it nothing opens');
  assert.equal(lists().length, 0);
  assert.equal(r.menu.track(130, 30), true);
  assert.equal(lists().length, 1, 'the row menu is up');
  assert.ok(btn.classList.contains('is-open'));
  assert.equal(lists()[0].style.left, '120px', 'anchored under the ⋯');
  assert.equal(lists()[0].style.top, '48px');
  r.layOut();
  const [open, , remove] = menuItems(lists()[0]);
  r.menu.track(130, 60);
  assert.ok(open.classList.contains('is-drag-hover'), 'the item under the held row is lit');
  r.menu.track(130, 120);
  assert.ok(!open.classList.contains('is-drag-hover') && remove.classList.contains('is-drag-hover'), 'the light moves with it');
  assert.deepEqual(r.ran, [], 'hovering runs nothing, however long');
  assert.equal(r.menu.applied, false);
  r.menu.end();
  assert.equal(lists().length, 0, 'the drag ending folds the menu');
  assert.equal(r.header.children.length, 2, 'and the ⋯ leaves with it');
});

test('a flyout item opens its list on hover, and a release on its own item runs it', () => {
  const r = mount();
  hold(r);
  const [, copy] = menuItems(lists()[0]);
  r.menu.track(130, 90);
  assert.ok(copy.classList.contains('is-drag-hover'));
  const sub = lists().find((l) => l.classList.contains('project-submenu'));
  assert.ok(sub, 'its list flew out');
  at(sub, box(304, 78, 160, 40));
  const [image] = menuItems(sub);
  at(image, box(309, 83, 150, 28));
  r.menu.track(320, 95);
  assert.ok(lists().some((l) => l === sub), 'moving into the list keeps it out');
  assert.equal(r.menu.drop(130, 90), true, 'a release on the opener is the menu\'s…');
  assert.deepEqual(r.ran, [], '…and runs nothing');
  r.menu.track(320, 95);
  assert.equal(r.menu.drop(320, 95), true);
  assert.deepEqual(r.ran, ['copy-image']);
  assert.equal(lists().length, 0, 'the pick closed the menu and its list');
  assert.equal(r.menu.applied, true);
  r.menu.end();
});

test('the gap under the ⋯ is no way out; leaving both folds the menu', () => {
  const r = mount();
  hold(r);
  assert.equal(r.menu.track(130, 45), true, 'between the ⋯ and its menu');
  assert.equal(lists().length, 1);
  r.menu.track(130, 60);
  assert.equal(r.menu.track(310 + DRAG_MENU_SLACK_PX, 60), false, 'past the slack');
  assert.equal(lists().length, 0, 'folded');
  r.menu.end();
});

test('a release runs the item under it; on the ⋯ it is claimed and runs nothing; elsewhere it is not the menu\'s', () => {
  const r = mount();
  hold(r);
  assert.equal(r.menu.drop(130, 120), true);
  assert.deepEqual(r.ran, ['remove']);
  assert.equal(r.menu.applied, true);
  assert.equal(r.menu.track(130, 30), false, 'the rest is inert');
  r.menu.end();

  const s = mount();
  s.menu.begin(s.items);
  at(s.header.children[1], box(120, 20, 30, 22));
  assert.equal(s.menu.drop(130, 30), true, 'the ⋯ owns its own drop');
  assert.deepEqual(s.ran, []);
  s.menu.end();

  const t = mount();
  hold(t);
  assert.equal(t.menu.drop(600, 400), false, 'a release elsewhere is not the menu\'s');
  assert.deepEqual(t.ran, []);
  t.menu.end();
});

test('a row without a menu brings no ⋯', () => {
  const r = mount();
  r.menu.begin(null);
  assert.equal(r.header.children.length, 2);
  assert.equal(r.menu.track(130, 30), false);
  assert.equal(r.menu.drop(130, 30), false);
  r.menu.end();
});

test('the ⋯ and its menu come and go on the surface dust, all of it out of and into the ⋯\'s centre', (t) => {
  setMotionOverride({ mode: 'particles' });
  t.after(() => setMotionOverride({ mode: 'none' }));
  const r = mount();
  r.menu.begin(r.items);
  const btn = r.header.children[1];
  assert.ok(btn.classList.contains(SURFACE_FORMING_CLASS), 'the ⋯ gathers out of dust');
  assert.ok(near(cloudAim(btn.__dustHost), { x: 50, y: 10 }), 'out of its own centre, where it lands');
  // Wide enough that its centre is well away from the corner the menu is placed at.
  at(btn, box(120, 20, 60, 30));
  r.menu.track(150, 35);
  const [menu] = lists();
  assert.equal([menu.style.left, menu.style.top].join(), '120px,56px', 'placed under the ⋯\'s left edge');
  assert.ok(menu.classList.contains(SURFACE_FORMING_CLASS), 'the menu forms as the row menu does');
  assert.ok(near(cloudAim(menu.__dustHost), { x: 150, y: 35 }), 'out of the ⋯\'s centre, not its placing corner');
  r.menu.track(800, 600);
  assert.ok(menu.__dustHost.className.includes('dust-leaving'), 'leaving both, it pours away…');
  assert.ok(near(cloudAim(menu.__dustHost), { x: 150, y: 35 }), '…back into the ⋯\'s centre');
  r.menu.end();
  assert.ok(btn.classList.contains(SURFACE_LEAVING_CLASS), 'the ⋯ leaves as dust');
  assert.ok(near(cloudAim(btn.__dustHost), { x: 150, y: 35 }), 'into its own centre');
});
