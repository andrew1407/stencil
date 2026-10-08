// A stub Lines tab for the tests/ui/panel/lines suites: the #lines-list table and its body under a
// document whose elements answer closest() over the stub tree, so a delegated event finds its row,
// and an app whose lines, selection and history the real list, picker and editors work on.
import { createStubElement } from './dom.js';

const HAS = (cls) => (n) => n.classList?.contains(cls);
const MATCH = {
  '.lines-remove': HAS('lines-remove'), '.lines-swatch': HAS('lines-swatch'),
  '.lines-point-swatch': HAS('lines-point-swatch'), '.lines-num': HAS('lines-num'),
  'tr.lines-row': (n) => n.tagName === 'TR' && n.classList?.contains('lines-row'),
};

const descendants = (el) => el.children.flatMap((c) => (c && typeof c === 'object' ? [c, ...descendants(c)] : []));

const withTree = (el) => {
  el.closest = (sel) => {
    for (let n = el; n; n = n.parentNode) if (MATCH[sel]?.(n)) return n;
    return null;
  };
  el.querySelector = (sel) => (sel === 'input' ? descendants(el).find((c) => c.tagName === 'INPUT') ?? null : null);
  el.replaceChildren = (...kids) => { el.children.length = 0; el.append(...kids); };
  el.showPicker = () => { el.picks = (el.picks ?? 0) + 1; };
  el.select = () => {};
  return el;
};

export const installLinesTab = () => {
  const body = createStubElement('tbody', {
    replaceChildren() { body.children.length = 0; },
    querySelectorAll: () => body.children.filter((r) => r.classList.contains('lines-row')),
    // `tr.lines-row[data-idx="i"] .cls`: the swatch a picker anchors to after a re-render.
    querySelector: (sel) => {
      const m = /data-idx="(\d+)"\] \.([\w-]+)/.exec(sel);
      const row = m && body.children.find((r) => r.dataset?.idx === m[1]);
      return row ? descendants(row).find((c) => c.classList?.contains(m[2])) ?? null : null;
    },
  });
  const table = createStubElement('table', { tBodies: [body] });
  globalThis.document = {
    getElementById: (id) => (id === 'lines-list' ? table : null),
    createElement: (tag) => withTree(createStubElement(tag)),
  };
  return { body, table };
};

const line = (color, extra = {}) => ({ points: [{ x: 0, y: 0 }, { x: 5, y: 5 }], color, thickness: 3, pointSize: 6, ...extra });

// Lines 1 and 3 multi-selected; `saved` counts history steps, `shown` the bar's re-shows.
export const makeLinesApp = () => {
  const app = {
    lines: [line('#ff0000'), line('', { pointColor: '#00ff00' }), line('#0000ff80')], color: '#00ff00', pointSize: 4,
    selectedLines: [0, 2], selectedLineIdx: -1, hoverLineIdx: -1, listHoverLineIdx: -1,
    coordLineIdx: -1, focusedPtIdx: -1, defaultFillColor: '#ffffff', saved: 0, shown: 0, readOnly: false,
    renderer: { redraw() {} }, coordTable: { update() {} },
    showSelectionPanel() { app.shown++; }, hideSelectionPanels() {},
    deselectLine() { app.selectedLineIdx = -1; app.selectedLines = []; }, saveHistory() { app.saved++; },
    compareReadOnly: () => app.readOnly,
  };
  return app;
};
