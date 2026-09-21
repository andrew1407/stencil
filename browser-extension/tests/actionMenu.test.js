// Tests for src/lib/actionMenu.js — the floating ⋯ action menu extracted from popup.js:
// pure placement math (menu flip/clamp, anchored-x, submenu flyout) and the factory's
// builders + open/close/Escape machinery, driven with a stub document.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { menuPlacement, anchoredX, flyoutPlacement, createActionMenu } from '../src/lib/control/actionMenu.js';
import { stubDoc, stubEl, stubWin } from './helpers/domStub.js';

// ── Pure placement ──

test('menuPlacement: fits as asked, clamps overflow, never leaves the margin', () => {
  const viewport = { width: 400, height: 600 };
  const size = { width: 200, height: 100 };
  assert.deepEqual(menuPlacement({ x: 50, y: 60, size, viewport }), { left: 50, top: 60 });
  // Overflowing right/bottom pulls back inside the 6px margin.
  assert.deepEqual(menuPlacement({ x: 350, y: 60, size, viewport }), { left: 400 - 200 - 6, top: 60 });
  assert.deepEqual(menuPlacement({ x: 50, y: 550, size, viewport }), { left: 50, top: 600 - 100 - 6 });
  // A menu larger than the viewport still lands at the margin, not negative.
  assert.deepEqual(menuPlacement({ x: 0, y: 0, size: { width: 500, height: 700 }, viewport }), { left: 6, top: 6 });
});

test('anchoredX prefers the left of the button and falls back to its right', () => {
  assert.equal(anchoredX({ rect: { left: 300, right: 320 }, width: 150 }), 300 - 150 - 6);
  // No room on the left → open to the right instead.
  assert.equal(anchoredX({ rect: { left: 100, right: 120 }, width: 150 }), 120 + 6);
});

test('flyoutPlacement opens right of the head and converts to wrap-relative coords', () => {
  const p = flyoutPlacement({
    head: { left: 40, right: 200, top: 100 },
    wrap: { left: 38, top: 96 },
    size: { width: 150, height: 120 },
    viewport: { width: 400, height: 600 },
  });
  assert.deepEqual(p, { left: (200 - 2) - 38, top: (100 - 5) - 96 });
});

test('flyoutPlacement flips left at the right edge and clamps both axes', () => {
  // Near the right edge: right side would overflow → open to the head's left.
  const flipped = flyoutPlacement({
    head: { left: 240, right: 390, top: 100 },
    wrap: { left: 238, top: 96 },
    size: { width: 150, height: 120 },
    viewport: { width: 400, height: 600 },
  });
  assert.equal(flipped.left, (240 - 150 + 2) - 238);
  // Deep in a short viewport: y clamps to keep the flyout fully inside.
  const clamped = flyoutPlacement({
    head: { left: 40, right: 200, top: 560 },
    wrap: { left: 38, top: 556 },
    size: { width: 150, height: 120 },
    viewport: { width: 400, height: 600 },
  });
  assert.equal(clamped.top, (600 - 120 - 6) - 556);
  // Wider than the viewport allows on either side: x pins at the margin.
  const pinned = flyoutPlacement({
    head: { left: 4, right: 8, top: 100 },
    wrap: { left: 0, top: 96 },
    size: { width: 500, height: 120 },
    viewport: { width: 400, height: 600 },
  });
  assert.equal(pinned.left, 6 - 0);
});

const build = ({ run } = {}) => {
  const doc = stubDoc();
  const menuEl = stubEl();
  menuEl.hidden = true;
  const menu = createActionMenu({ menuEl, run, doc, win: stubWin() });
  return { doc, menuEl, menu };
};

// ── Builders ──

test('item: a button with the icon span and a TEXT-NODE label; click closes then runs', async () => {
  const ran = [];
  const { menuEl, menu } = build({ run: async (fn) => { ran.push('run'); await fn(); } });
  const b = menu.item('<svg/>', 'Open <b>x</b>', () => ran.push('action'));
  assert.equal(b.type, 'button');
  const [ic, labelNode] = b.children;
  assert.equal(ic.className, 'ic');
  assert.equal(ic.innerHTML, '<svg/>');
  // The label must be a text node — page-derived names never reach innerHTML.
  assert.equal(labelNode.isText, true);
  assert.equal(labelNode.text, 'Open <b>x</b>');
  menuEl.hidden = false;
  await b.fire('click');
  assert.deepEqual(ran, ['run', 'action']);
  assert.equal(menuEl.hidden, true, 'the click closed the menu before running');
});

test('sep and label build the styled dividers; label text stays data', () => {
  const { menu } = build();
  assert.equal(menu.sep().className, 'sep');
  const l = menu.label('Shared <i>from</i> server');
  assert.equal(l.className, 'label');
  assert.equal(l.textContent, 'Shared <i>from</i> server');
  assert.equal(l.innerHTML, '');
});

test('submenu: head + flyout with the children, repositioned on hover by the pure math', () => {
  const { menu } = build();
  const child = stubEl('button');
  const wrap = menu.submenu('<ic/>', 'Open', [child]);
  assert.equal(wrap.className, 'submenu');
  const [head, fly] = wrap.children;
  assert.equal(head.className, 'submenu-head');
  assert.match(head.innerHTML, /<span class="submenu-label">Open<\/span>/);
  assert.match(head.innerHTML, /class="caret"/);
  assert.equal(fly.className, 'flyout');
  assert.deepEqual(fly.children, [child]);

  head.rect = { left: 40, right: 200, top: 100 };
  wrap.rect = { left: 38, top: 96 };
  fly.offsetWidth = 150;
  fly.offsetHeight = 120;
  wrap.fire('mouseenter');
  assert.equal(fly.style.margin, '0');
  assert.equal(fly.style.left, `${(200 - 2) - 38}px`);
  assert.equal(fly.style.top, `${(100 - 5) - 96}px`);
});

// ── Open / close machinery ──

test('openAnchored: marks the anchor, fills AFTER setting it, places left of the button', () => {
  const { menuEl, menu } = build();
  const btn = stubEl('button');
  btn.rect = { left: 300, right: 320, top: 150, width: 20, height: 20 };
  menuEl.offsetWidth = 150;
  menuEl.offsetHeight = 100;
  let anchorDuringFill = null;
  menu.openAnchored(btn, () => { anchorDuringFill = menu.anchor(); });
  assert.equal(anchorDuringFill, btn, 'buildMenu can capture the anchor');
  assert.equal(btn.classList.contains('active'), true);
  assert.equal(menuEl.hidden, false);
  assert.equal(menuEl.style.left, `${300 - 150 - 6}px`);
  assert.equal(menuEl.style.top, '150px');
  assert.ok(menuEl.style.transformOrigin, 'the pop animation grows from the button');
});

test('openAnchored on the same button toggles the menu closed', () => {
  const { menuEl, menu } = build();
  const btn = stubEl('button');
  btn.rect = { left: 300, right: 320, top: 150, width: 20, height: 20 };
  menu.openAnchored(btn, () => {});
  assert.equal(menuEl.hidden, false);
  menu.openAnchored(btn, () => {});
  assert.equal(menuEl.hidden, true);
  assert.equal(btn.classList.contains('active'), false);
  assert.equal(menu.anchor(), null);
});

test('openNodes appends the caller-built items into the shared element', () => {
  const { menuEl, menu } = build();
  const btn = stubEl('button');
  btn.rect = { left: 300, right: 320, top: 150, width: 20, height: 20 };
  const a = stubEl('button');
  const b = stubEl('button');
  menu.openNodes(btn, [a, b]);
  assert.deepEqual(menuEl.children, [a, b]);
});

test('openAt places at the point with no anchor; Escape (capture) closes and disarms', () => {
  const { doc, menuEl, menu } = build();
  menuEl.offsetWidth = 150;
  menuEl.offsetHeight = 100;
  let filled = false;
  menu.openAt(50, 60, () => { filled = true; });
  assert.equal(filled, true);
  assert.equal(menuEl.hidden, false);
  assert.equal(menu.anchor(), null);
  assert.equal(menuEl.style.left, '50px');
  assert.equal(menuEl.style.top, '60px');
  const esc = doc.listeners.find((l) => l.type === 'keydown');
  assert.equal(esc.capture, true, 'armed capture-phase so it wins over the surface');
  let stopped = 0;
  esc.fn({ key: 'a', preventDefault: () => {}, stopPropagation: () => stopped++ });
  assert.equal(menuEl.hidden, false, 'other keys pass through');
  esc.fn({ key: 'Escape', preventDefault: () => {}, stopPropagation: () => stopped++ });
  assert.equal(menuEl.hidden, true);
  assert.equal(stopped, 1);
  assert.equal(doc.listeners.length, 0, 'closing disarms the Escape listener');
});

test('close resets the element for the next owner', () => {
  const { menuEl, menu } = build();
  const btn = stubEl('button');
  btn.rect = { left: 300, right: 320, top: 150, width: 20, height: 20 };
  menu.openNodes(btn, [stubEl('button')]);
  menu.close();
  assert.equal(menuEl.hidden, true);
  assert.equal(menuEl.innerHTML, '');
  assert.deepEqual(menuEl.children, []);
  assert.equal(btn.classList.contains('active'), false);
});
