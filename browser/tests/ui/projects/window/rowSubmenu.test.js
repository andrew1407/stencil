// A row menu's nested list (ui/projects/window/rowSubmenu.js): it flies out beside its item on
// click, folds when another item is hovered, runs a nested pick after closing everything, and
// a press inside it never reads as a click-away.
import test from 'node:test';
import assert from 'node:assert';
import { createStubElement } from '../../../helpers/dom.js';
import { installDustDom, rect } from '../../../helpers/dustCloudRig.js';

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
