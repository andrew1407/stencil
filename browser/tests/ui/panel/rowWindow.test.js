// js/ui/panel/rowWindow.js: a long table body is the rows its scroller shows (and an overscan) between
// two spacers whose heights stand in for the rest; a short one is left to its caller as it always was.
import { test } from 'node:test';
import assert from 'node:assert/strict';

const ROW_PX = 28;   // the window's first guess, so a stride that matches it renders once
const HEAD_PX = 30;
const node = (tag) => ({
  tag, style: { setProperty() {} }, dataset: {}, children: [], attrs: {},
  setAttribute(k, v) { this.attrs[k] = v; }, appendChild(c) { this.children.push(c); return c; },
});
globalThis.document = {
  createElement: (tag) => node(tag),
  createDocumentFragment: () => node('#fragment'),
};
const { RowWindow, VIRTUAL_ROWS } = await import('../../../js/ui/panel/rowWindow.js');

const rig = ({ clientHeight = 280 } = {}) => {
  const listeners = {};
  const scroller = { scrollTop: 0, clientHeight, addEventListener: (t, fn) => { listeners[t] = fn; },
    getBoundingClientRect: () => ({ top: 100 }) };
  const body = {
    children: [], parentElement: { tHead: { offsetHeight: HEAD_PX } },
    closest: (sel) => (sel === '#coord-body' ? scroller : null),
    getBoundingClientRect: () => ({ top: 100 + HEAD_PX - scroller.scrollTop }),
    replaceChildren(frag) { this.children = frag.children; },
    querySelector(sel) {
      if (sel === 'input') return this.editing ? {} : null;
      const i = /"(\d+)"/.exec(sel)?.[1];
      return this.children.find((r) => r.dataset.ptIdx === i) ?? null;
    },
    querySelectorAll() { return this.children.filter((r) => r.dataset.ptIdx !== undefined); },
  };
  const built = [];
  const win = new RowWindow(body, { columns: 6, indexAttr: 'data-pt-idx', build: (i) => {
    built.push(i);
    const row = node('tr');
    row.dataset.ptIdx = String(i);
    row.offsetTop = i * ROW_PX;
    return row;
  } });
  const indices = () => body.children.filter((r) => r.dataset.ptIdx !== undefined).map((r) => Number(r.dataset.ptIdx));
  const spacers = () => body.children.filter((r) => r.className === 'row-window-spacer');
  return { scroller, body, win, built, indices, spacers, scroll: () => listeners.scroll() };
};

test('a list of VIRTUAL_ROWS or fewer is the caller\'s to list in full', () => {
  const { win, body } = rig();
  assert.equal(win.show(VIRTUAL_ROWS), false);
  assert.equal(win.active, false);
  assert.deepEqual(body.children, []);
});

test('a long list renders the rows on screen between spacers that keep its full height', () => {
  const { win, indices, spacers } = rig();
  const count = 10000;
  assert.equal(win.show(count), true);
  const shown = indices();
  assert.equal(shown[0], 0);
  assert.ok(shown.length < 60, `${shown.length} rows, not ${count}`);
  assert.equal(spacers().length, 1, 'at the top there is nothing above');
  assert.equal(spacers()[0].children[0].style.cssText, `height:${(count - shown.length) * ROW_PX}px;padding:0;border:0;`);
});

test('a scroll renders the rows it reaches, each on its own stripe', () => {
  const { win, scroller, indices, spacers, scroll } = rig();
  win.show(10000);
  scroller.scrollTop = 5000 * ROW_PX;
  scroll();
  const shown = indices();
  assert.ok(shown[0] <= 5000 && shown[shown.length - 1] >= 5010, `${shown[0]}…${shown[shown.length - 1]}`);
  assert.equal(shown[0] % 2, 1, 'a first row after a spacer is odd, so nth-child parity follows the index');
  const [above, below] = spacers();
  assert.equal(above.children[0].style.cssText, `height:${shown[0] * ROW_PX}px;padding:0;border:0;`);
  assert.equal(below.children[0].style.cssText, `height:${(10000 - 1 - shown[shown.length - 1]) * ROW_PX}px;padding:0;border:0;`);
});

test('an open cell editor holds its rows through a scroll', () => {
  const { win, scroller, body, indices, scroll } = rig();
  win.show(10000);
  const before = indices();
  body.editing = true;
  scroller.scrollTop = 4000 * ROW_PX;
  scroll();
  assert.deepEqual(indices(), before);
});

test('reveal scrolls a far row clear of the sticky header and hands it back', () => {
  const { win, scroller } = rig();
  win.show(10000);
  const row = win.reveal(9000);
  assert.equal(row?.dataset.ptIdx, '9000');
  assert.ok(scroller.scrollTop + scroller.clientHeight >= HEAD_PX + 9001 * ROW_PX, 'its bottom is in view');
  assert.equal(win.reveal(10)?.dataset.ptIdx, '10');
  assert.equal(scroller.scrollTop, 10 * ROW_PX, 'back up: the row sits right under the header');
});

test('a sizer row goes last, after the spacers, and only for a long list', () => {
  const { body } = rig();
  const sizer = node('tr');
  const win = new RowWindow(body, { columns: 6, indexAttr: 'data-pt-idx', sizer: () => sizer,
    build: (i) => Object.assign(node('tr'), { dataset: { ptIdx: String(i) }, offsetTop: i * ROW_PX }) });
  win.show(10000);
  assert.equal(body.children[body.children.length - 1], sizer);
  assert.equal(win.show(10), false);
});
