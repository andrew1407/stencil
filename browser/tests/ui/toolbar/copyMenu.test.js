// The Image section's "Make a copy" list (ui/toolbar/copyMenu.js) and the canvas menu's twin
// rows (ui/contextMenu/markup.js): three scopes in one order, each opening the confirmation.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { installDom } from '../../helpers/dom.js';

const opened = [];
const doc = installDom({}, {
  window: { innerWidth: 1200, innerHeight: 800, addEventListener() {}, removeEventListener() {} },
});
doc.querySelector = (sel) => (sel === 'stencil-copy-project-modal' ? { openFor: (...a) => opened.push(a) } : null);

const { wireCopyProjectMenu } = await import('../../../js/ui/toolbar/copyMenu.js');
const { contextMenuInner } = await import('../../../js/ui/contextMenu/markup.js');

const makeTrigger = () => {
  let menu = null;
  const el = doc.createElement('button');
  Object.assign(el, {
    insertAdjacentElement: (_pos, node) => { menu = node; },
    matches: () => false,
    contains: (t) => t === el,
    getBoundingClientRect: () => ({ left: 10, top: 10, right: 40, bottom: 40, width: 30, height: 30 }),
  });
  return { el, menuOf: () => menu };
};

test('a click lists the three scopes; a row opens the confirmation for the live editor', () => {
  const { el, menuOf } = makeTrigger();
  wireCopyProjectMenu(el, { image: {} });
  el.dispatch('pointerdown', { clientX: 0, clientY: 0, pointerType: 'mouse' });
  el.dispatch('click');
  const menu = menuOf();
  assert.equal(menu.hidden, false, 'the list opened on the first click');
  assert.deepEqual(menu.children.map((r) => r.dataset.copyScope), ['image', 'layout', 'project']);
  assert.match(menu.children[0].innerHTML, /Image only/);
  menu.children[1].dispatch('click');
  assert.equal(menu.hidden, true, 'a pick closes the list');
  assert.deepEqual(opened.at(-1)[0], { id: null, what: 'layout' });
  assert.equal(opened.at(-1)[1].backTo, el, 'the confirmation closes back into the button');
});

test('no image, no list', () => {
  const { el, menuOf } = makeTrigger();
  wireCopyProjectMenu(el, { image: null });
  el.dispatch('click');
  assert.equal(menuOf().hidden, true);
});

test('the canvas menu carries Make a Copy after Image / Layout, before Fullscreen', () => {
  const html = contextMenuInner();
  const at = (id) => html.indexOf(`id="${id}"`);
  assert.ok(at('ctx-ul-layout') < at('ctx-copy-project-menu'));
  assert.ok(at('ctx-copy-project-menu') < at('ctx-fullscreen'));
  const rows = [...html.matchAll(/data-copy-scope="(\w+)"/g)].map((m) => m[1]);
  assert.deepEqual(rows, ['image', 'layout', 'project']);
  assert.match(html, /Image and Layout/);
});
