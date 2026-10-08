// A held project row and the window's header (js/ui/projects/list/dragReorder.js), on the real
// projects modal: hovering the drag-time ⋯ menu only lights an item; released on one, the item runs
// and that drop reorders nothing, runs no zone and does not re-list under the question it raised —
// and the open project dropped on the ✕ is closed here at once, the window staying up.
import test from 'node:test';
import assert from 'node:assert/strict';
import { mountProjectsModal } from '../../../helpers/projectsModalRig.js';
import { TARGET_CLASS, TARGET_OVER_CLASS } from '../../../../js/ui/drag/iconDrag.js';

globalThis.MouseEvent = class { constructor(type, init = {}) { this.type = type; Object.assign(this, init); } };
const METAS = [{ id: 'a', name: 'Alpha', updatedAt: 2 }, { id: 'b', name: 'Beta', updatedAt: 1 }];
const box = (left, top, width, height) => ({ left, top, width, height, right: left + width, bottom: top + height });
const at = (el, r) => { el.getBoundingClientRect = () => r; return el; };
const dt = () => ({ setData() {}, setDragImage() {}, effectAllowed: '', dropEffect: '' });
const ev = (x, y) => ({ clientX: x, clientY: y, dataTransfer: dt(), prevented: false,
  preventDefault() { this.prevented = true; }, stopPropagation() {} });

const mount = async (t) => {
  const asked = [];
  let answer = null;
  const calls = [];
  const app = {
    activeProjectId: 'a',
    confirm: (message, opts) => new Promise((resolve) => { asked.push({ message, opts }); answer = resolve; }),
    closeProject: (id) => calls.push(['closeProject', id]),
    projectTransfer: new Proxy({}, { get: (_, name) => (...args) => calls.push([name, ...args]) }),
  };
  const m = await mountProjectsModal(t, { metas: METAS, mode: 'none', app });
  m.change('projects-filter', 'all');
  // The card is the middle of the page; the header holds the title, then (while held) the ⋯, then ✕.
  at(m.doc.querySelector('.app-modal'), box(200, 0, 600, 700));
  const header = m.doc.querySelector('#projects-modal-overlay .settings-header');
  at(m.$('projects-close'), box(700, 10, 70, 24));
  const row = (key) => m.rows().find((r) => r.dataset.filterKey === key);
  const hold = (key) => { row(key).dispatch('dragstart', ev(260, 200)); return row(key); };
  const over = (x, y) => m.doc.dispatch('dragover', ev(x, y));
  return { m, app, asked, calls, header, row, hold, over, answer: (v) => answer(v), timers: m.timers };
};

test('a release on a ⋯ item runs it, and that drop reorders nothing, runs no zone, re-lists nothing', async (t) => {
  const r = await mount(t);
  const source = r.hold('local:b');
  const more = r.header.children.find((c) => c.classList.contains('projects-drag-more'));
  assert.ok(more, 'the ⋯ formed by the title');
  assert.equal(r.header.children.indexOf(more), 1, 'right after it');
  at(more, box(320, 10, 30, 22));
  assert.equal(r.over(330, 20).prevented, true, 'the ⋯ accepts the drop');
  const menu = r.m.doc.querySelector('.project-menu');
  assert.ok(menu, 'Beta\'s own menu opened');
  at(menu, box(320, 38, 180, 260));
  const remove = menu.children.find((b) => b.textContent.includes('Remove'));
  at(remove, box(325, 100, 170, 28));
  assert.equal(r.over(330, 110).prevented, true, 'its items accept the drop');
  assert.ok(remove.classList.contains('is-drag-hover'), 'hovering lights Remove…');
  r.timers.splice(0).forEach((x) => x.fn());
  assert.equal(r.asked.length, 0, '…and runs nothing, however long it rests');
  const shown = r.m.rows();
  assert.equal(r.m.doc.dispatch('drop', ev(330, 110)).prevented, true);
  assert.equal(r.asked.length, 1, 'released there, Remove asked, as its click does');
  assert.match(r.asked[0].message, /Remove project "Beta"/);
  assert.equal(r.m.doc.querySelector('.project-menu'), null, 'the menu closed');
  source.dispatch('dragend', ev(330, 110));
  assert.notEqual(r.m.session.get('stencil_projects_sortmode'), 'manual', 'no reorder was saved');
  assert.deepEqual(r.calls, [], 'no zone action ran');
  assert.ok(r.m.rows().every((el, i) => el === shown[i]), 'the list was not re-listed under the open question');
  assert.equal(r.header.children.includes(more), false, 'the ⋯ left with the drag');
});

test('released anywhere but the menu, a ⋯ hover does nothing with it', async (t) => {
  const r = await mount(t);
  const source = r.hold('local:b');
  const more = r.header.children.find((c) => c.classList.contains('projects-drag-more'));
  at(more, box(320, 10, 30, 22));
  r.over(330, 20);
  const menu = r.m.doc.querySelector('.project-menu');
  at(menu, box(320, 38, 180, 260));
  at(menu.children.find((b) => b.textContent.includes('Remove')), box(325, 100, 170, 28));
  r.over(330, 110);
  r.over(700, 500);
  assert.equal(r.m.doc.querySelector('.project-menu'), null, 'leaving folds the menu');
  r.m.doc.dispatch('drop', ev(700, 500));
  source.dispatch('dragend', ev(700, 500));
  assert.equal(r.asked.length, 0, 'nothing the menu holds ran');
});

test('the open project dropped on the ✕ is closed here at once, unasked; the window stays', async (t) => {
  const r = await mount(t);
  const close = r.m.$('projects-close');
  const source = r.hold('local:a');
  assert.ok(close.classList.contains(TARGET_CLASS), 'the ✕ reads as a target for the open project');
  assert.equal(r.over(730, 20).prevented, true);
  assert.ok(close.classList.contains(TARGET_OVER_CLASS));
  assert.equal(r.m.doc.dispatch('drop', ev(730, 20)).prevented, true, 'the ✕ took the drop');
  source.dispatch('dragend', ev(730, 20));
  await new Promise((res) => setImmediate(res));
  assert.equal(r.asked.length, 0, 'nothing is asked');
  assert.deepEqual(r.calls, [['closeProject', 'a']]);
  assert.ok(r.m.$('projects-modal-overlay'), 'the projects window is still there');
  assert.ok(!close.classList.contains(TARGET_CLASS), 'and its ✕ is a plain button again');
});

test('another project leaves the ✕ inert: no glow, no claim, nothing closed', async (t) => {
  const r = await mount(t);
  const close = r.m.$('projects-close');
  const source = r.hold('local:b');
  assert.ok(!close.classList.contains(TARGET_CLASS));
  assert.equal(r.over(730, 20).prevented, false);
  r.m.doc.dispatch('drop', ev(730, 20));
  source.dispatch('dragend', ev(730, 20));
  assert.equal(r.asked.length, 0);
  assert.deepEqual(r.calls, []);
});
