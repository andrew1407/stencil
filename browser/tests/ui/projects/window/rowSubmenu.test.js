// A row menu's nested list (ui/projects/window/rowSubmenu.js): it flies out beside its item on
// click, folds when another item is hovered, runs a nested pick after closing everything, and
// a press inside it never reads as a click-away; its ⋯ toggles the menu.
import test from 'node:test';
import assert from 'node:assert';
import { createStubElement } from '../../../helpers/dom.js';
import { installDustDom, rect } from '../../../helpers/dustRig.js';

const dust = installDustDom({
  docOpts: { autoCreateById: true },
  globals: { window: { innerWidth: 1200, innerHeight: 800, addEventListener() {}, removeEventListener() {}, dispatchEvent() {} } },
});
const { doc } = dust;
doc.createElement = (tag) => {
  const el = createStubElement(tag, {
    focus() {}, getBoundingClientRect: () => rect(100, 100, 160, 30),
    querySelector: () => el.children.find((c) => c.tagName === 'BUTTON') ?? null,
    contains: (t) => t === el || el.children.includes(t),
  });
  return el;
};

const { createProjectRowMenu } = await import('../../../../js/ui/projects/window/projectRowMenu.js');
const ev = (extra = {}) => ({ preventDefault() {}, stopPropagation() {}, ...extra });
const menus = () => doc.body.children.filter((c) => c.classes?.has('project-menu'));

test('a nested item flies its list out beside it and runs a pick after closing both', async () => {
  const picked = [];
  const rows = createProjectRowMenu();
  const dots = createStubElement('button', { getBoundingClientRect: () => rect(500, 100, 24, 24) });
  rows.showMenu(dots, [
    { icon: 'folder', label: 'Open', onClick: () => picked.push('open') },
    { icon: 'duplicate', label: 'Make a copy', items: [
      { icon: 'image', label: 'Image only', onClick: (at) => picked.push(['image', !!at]) },
    ] },
  ]);
  const [menu] = menus();
  const [openBtn, copyBtn] = menu.children;
  assert.ok(copyBtn.classes.has('has-sub'), 'the nesting item is marked');
  assert.match(copyBtn.innerHTML, /project-menu-arrow/, 'and carries a chevron');

  copyBtn.dispatch('click', ev());
  const sub = menus().find((m) => m.classes.has('project-submenu'));
  assert.ok(sub, 'the nested list is out');
  assert.ok(copyBtn.classes.has('is-open'));

  copyBtn.dispatch('mouseenter');
  copyBtn.dispatch('click', ev());
  assert.ok(menus().some((m) => m.classes.has('project-submenu')), 'a click after the hover keeps it out');

  openBtn.dispatch('mouseenter');
  assert.equal(menus().some((m) => m.classes.has('project-submenu')), false, 'hovering a sibling folds it');

  copyBtn.dispatch('mouseenter');
  const again = menus().find((m) => m.classes.has('project-submenu'));
  again.children[0].dispatch('click', ev());
  assert.deepEqual(picked, [['image', true]]);
  assert.equal(menus().length, 0, 'the pick closed the menu and its list');
});

test('ArrowRight flies the list out onto its first item; ArrowLeft and Escape fold it back to the item', () => {
  const rows = createProjectRowMenu();
  rows.showMenu(createStubElement('button', { getBoundingClientRect: () => rect(500, 100, 24, 24) }), [
    { icon: 'duplicate', label: 'Make a copy', items: [{ icon: 'image', label: 'Image only', onClick() {} }] },
  ]);
  const [copyBtn] = menus()[0].children;
  const focused = [];
  copyBtn.focus = () => focused.push('item');
  const sub = () => menus().find((m) => m.classes.has('project-submenu'));
  for (const key of ['ArrowLeft', 'Escape']) {
    copyBtn.dispatch('keydown', ev({ key: 'ArrowRight' }));
    const first = sub().children[0];
    first.dispatch('keydown', ev({ key: 'ArrowDown' }));
    assert.ok(sub(), `${key}: another key leaves the list out`);
    first.dispatch('keydown', ev({ key }));
    assert.equal(sub(), undefined, `${key} folds the list`);
    assert.ok(!copyBtn.classes.has('is-open'));
  }
  assert.deepEqual(focused, ['item', 'item'], 'focus goes back to the item each time');
  rows.closeMenu?.();
});

test('its own ⋯ toggles it: a press there is not a click-away, and the click closes it', async () => {
  const rows = createProjectRowMenu();
  const dots = createStubElement('button', { getBoundingClientRect: () => rect(500, 100, 24, 24) });
  dots.contains = (t) => t === dots;
  const items = [{ icon: 'folder', label: 'Open', onClick() {} }];
  rows.showMenu(dots, items);
  await new Promise((r) => setTimeout(r, 0));
  assert.equal(menus().length, 1);
  doc.dispatch('mousedown', ev({ target: dots }));
  assert.equal(menus().length, 1, 'the press on its own ⋯ leaves it for the click');
  rows.showMenu(dots, items);
  assert.equal(menus().length, 0, 'the click closes it');
  rows.showMenu(dots, items);
  assert.equal(menus().length, 1, 'and the next one opens it again');
  rows.showMenu(dots, items, { x: 300, y: 40 });
  assert.equal(menus().length, 1, 'a right-click on the row reopens it at the cursor');
  doc.dispatch('mousedown', ev({ target: dots }));
  rows.showMenu(dots, items);
  assert.equal(menus().length, 1, 'the ⋯ then opens it again from itself rather than closing it');
  rows.showMenu(dots, items);
  assert.equal(menus().length, 0, 'and now toggles it shut');
});

test('a list nests a second list (Copy image ›): a press inside it is no click-away, and its pick closes all three', async () => {
  const picked = [];
  const rows = createProjectRowMenu();
  rows.showMenu(createStubElement('button', { getBoundingClientRect: () => rect(500, 100, 24, 24) }), [
    { icon: 'copy', label: 'Copy', items: [
      { icon: 'file-text', label: 'Layout', onClick: () => picked.push('layout') },
      { icon: 'image', label: 'Image', items: [{ icon: 'image', label: 'Current', onClick: () => picked.push('current') }] },
    ] },
  ]);
  await new Promise((r) => setTimeout(r, 0));
  menus()[0].children[0].dispatch('click', ev());
  const [layoutBtn, imageBtn] = menus()[1].children;
  imageBtn.dispatch('mouseenter');
  assert.equal(menus().length, 3, 'the second level is out beside the first');
  const current = menus()[2].children[0];
  doc.dispatch('mousedown', ev({ target: current }));
  assert.equal(menus().length, 3, 'a press in the second level does not close the menu');
  layoutBtn.dispatch('mouseenter');
  assert.equal(menus().length, 2, 'hovering a plain sibling folds the second level only');
  imageBtn.dispatch('mouseenter');
  menus()[2].children[0].dispatch('click', ev());
  assert.deepEqual(picked, ['current']);
  assert.equal(menus().length, 0, 'the pick closed every level');
});

test('an item with onDoubleClick waits out the double-click window: one click runs onClick, two run onDoubleClick', (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  const picked = [];
  const rows = createProjectRowMenu();
  const open = () => rows.showMenu(createStubElement('button', { getBoundingClientRect: () => rect(500, 100, 24, 24) }), [
    { icon: 'link', label: 'Project link', onClick: () => picked.push('copy'), onDoubleClick: () => picked.push('share') },
  ]);
  open();
  menus()[0].children[0].dispatch('click', ev({ detail: 1 }));
  assert.deepEqual(picked, [], 'nothing runs inside the window');
  t.mock.timers.tick(1000);
  assert.deepEqual(picked, ['copy']);
  open();
  const btn = menus()[0].children[0];
  btn.dispatch('click', ev({ detail: 1 }));
  btn.dispatch('click', ev({ detail: 2 }));
  t.mock.timers.tick(1000);
  assert.deepEqual(picked, ['copy', 'share'], 'the second click inside the window is the double, once');
  assert.equal(menus().length, 0);
});

test('one flyout out per level: hovering a sibling flyout item folds the open one, top level and nested (user report)', () => {
  const rows = createProjectRowMenu();
  rows.showMenu(createStubElement('button', { getBoundingClientRect: () => rect(500, 100, 24, 24) }), [
    { icon: 'copy', label: 'Copy entries', items: [
      { icon: 'image', label: 'A', items: [{ icon: 'image', label: 'A1', onClick() {} }] },
      { icon: 'image', label: 'B', items: [{ icon: 'image', label: 'B1', onClick() {} }] },
    ] },
    { icon: 'image', label: 'Copy image', items: [{ icon: 'image', label: 'Current', onClick() {} }] },
  ]);
  const [entries, image] = menus()[0].children;
  const subs = () => menus().filter((m) => m.classes.has('project-submenu'));
  entries.dispatch('mouseenter');
  assert.equal(subs().length, 1);
  image.dispatch('mouseenter');
  assert.equal(subs().length, 1, 'Copy entries folded when Copy image opened');
  assert.ok(!entries.classes.has('is-open') && image.classes.has('is-open'));
  entries.dispatch('mouseenter');
  const [a, b] = subs()[0].children;
  a.dispatch('mouseenter');
  assert.equal(subs().length, 2);
  b.dispatch('mouseenter');
  assert.equal(subs().length, 2, 'the nested A folded when B opened');
  assert.ok(!a.classes.has('is-open') && b.classes.has('is-open'));
  rows.closeMenu?.();
});
