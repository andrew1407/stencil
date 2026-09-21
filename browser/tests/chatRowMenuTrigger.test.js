// The hover "…" trigger and the touch routes (js/ui/chatView.js): one per settled row on the
// corner facing the panel centre, plus long-press and double-tap where there is no hover.
import { test } from 'node:test';
import assert from 'node:assert';
import { chatViewSource } from './helpers/chatViewSource.js';
import { stubDom, menuOn, itemLabels, wiredRow } from './helpers/chatRowMenuRig.js';

// ── The hover "…" trigger ──
test('chatRowMenuButton: a trigger per settled row, on the corner facing the panel centre', async () => {
  stubDom();
  const { chatRowMenuButton } = await import('../js/ui/chat/chatView.js?rowmenu-btn');
  const user = chatRowMenuButton({ role: 'user', text: 'hi' });
  assert.ok(String(user.className).split(/\s+/).includes('chat-row-menu-btn'));
  assert.ok(String(user.className).includes('chat-row-menu-btn-left'),
    'user bubbles are right-aligned, so their button sits bottom-LEFT');
  assert.ok(user.innerHTML.includes('ic-more'), 'the shared "⋯" glyph');
  const asst = chatRowMenuButton({ role: 'assistant', text: 'yo' });
  assert.ok(String(asst.className).includes('chat-row-menu-btn-right'),
    'assistant bubbles are left-aligned, so bottom-RIGHT');
  // Pending rows have no menu (chatRowMenuItems is []), so no trigger either.
  assert.strictEqual(chatRowMenuButton({ role: 'assistant', text: '…', pending: true }), null);
  assert.strictEqual(chatRowMenuButton(null), null);
  // renderChatLog appends it per repaint (a text rewrite wipes the row's children).
  const view = chatViewSource();
  assert.ok(view.includes("if (!el.querySelector('.chat-row-menu-btn'))"));
  assert.ok(view.includes('chatRowMenuButton(row)'));
});

test('clicking the "…" button opens the SAME menu as right-click, anchored at the button', async () => {
  const row = { role: 'user', text: 'hello' };
  const { body, transcript, rowEl } = await wiredRow(row, {}, '-btn-open');
  const { chatRowMenuButton } = await import('../js/ui/chat/chatView.js?rowmenu-btn-open');
  const btn = rowEl.appendChild(chatRowMenuButton(row));
  transcript.fire('click', { target: btn, preventDefault() {}, stopPropagation() {} });
  const menu = menuOn(body);
  assert.ok(menu, 'the button opens the floating menu');
  assert.deepStrictEqual(itemLabels(menu), ['Copy message', 'Insert into prompt', 'Resend'],
    'the exact right-click items');
});

// ── Touch gestures (no hover there) ──
test('touchMenuGesture: long-press fires at the threshold; movement or early release cancels', async () => {
  const { touchMenuGesture } = await import('../js/ui/chat/chatView.js?rowmenu-touch');
  const opened = [];
  let armed = null;
  const g = touchMenuGesture((x, y) => opened.push([x, y]), {
    setTimer: (fn, ms) => { armed = { fn, ms }; return 1; },
    clearTimer: () => { armed = null; },
  });
  // Held past the threshold: opens at the touch point, and the release reports it.
  g.start('rowA', 30, 40);
  assert.strictEqual(armed.ms, 500, 'the long-press threshold is 500ms');
  armed.fn();
  assert.deepStrictEqual(opened, [[30, 40]]);
  assert.strictEqual(g.end(0), true, '…so the caller suppresses the native callout');
  // Jitter inside the 10px tolerance keeps it armed; drifting past it cancels.
  opened.length = 0;
  g.start('rowA', 30, 40);
  g.move(35, 44);
  assert.ok(armed, 'sub-tolerance jitter keeps the press armed');
  g.move(30, 60);
  assert.strictEqual(armed, null, 'a drag past 10px is a scroll — press cancelled');
  assert.strictEqual(g.end(10), false);
  // Early release: a plain tap opens nothing.
  g.start('rowB', 1, 2);
  assert.strictEqual(g.end(1000), false);
  assert.deepStrictEqual(opened, []);
});

test('touchMenuGesture: two quick taps on the same row open; slow or cross-row taps don\'t', async () => {
  const { touchMenuGesture } = await import('../js/ui/chat/chatView.js?rowmenu-touch2');
  const opened = [];
  const g = touchMenuGesture((x, y) => opened.push([x, y]), { setTimer: () => 1, clearTimer: () => {} });
  g.start('rowA', 10, 20);
  assert.strictEqual(g.end(100), false, 'the first tap only waits');
  g.start('rowA', 12, 22);
  assert.strictEqual(g.end(300), true, 'a second tap within 350ms opens');
  assert.deepStrictEqual(opened, [[12, 22]], '…at the second tap\'s point');
  // 400ms apart: two singles.
  opened.length = 0;
  g.start('rowA', 0, 0); g.end(1000);
  g.start('rowA', 0, 0);
  assert.strictEqual(g.end(1400), false, 'taps 400ms apart never pair');
  // A tap on ANOTHER row starts over.
  g.start('rowA', 0, 0); g.end(2000);
  g.start('rowB', 0, 0);
  assert.strictEqual(g.end(2100), false, 'cross-row taps never pair');
  assert.deepStrictEqual(opened, []);
});

test('double-tap on a bubble opens the menu through the transcript wiring; pending rows stay inert', async () => {
  const row = { role: 'user', text: 'yo' };
  const { body, transcript, rowEl } = await wiredRow(row, {}, '-touch-wire');
  const tap = (target) => {
    transcript.fire('touchstart', { touches: [{ clientX: 15, clientY: 25 }], target });
    const ev = { prevented: false, preventDefault() { this.prevented = true; } };
    transcript.fire('touchend', ev);
    return ev;
  };
  const first = tap(rowEl);
  assert.strictEqual(menuOn(body), null, 'one tap opens nothing');
  assert.strictEqual(first.prevented, false, '…and native behavior is untouched');
  const second = tap(rowEl);
  assert.ok(menuOn(body), 'the second quick tap opens the menu');
  assert.strictEqual(second.prevented, true, 'only then is the native callout suppressed');
  assert.deepStrictEqual(itemLabels(menuOn(body)), ['Copy message', 'Insert into prompt', 'Resend']);
  // A pending row never opens, however many taps land on it.
  const pend = await wiredRow({ role: 'assistant', text: '…', pending: true }, {}, '-touch-pend');
  const tapPend = () => {
    pend.transcript.fire('touchstart', { touches: [{ clientX: 5, clientY: 5 }], target: pend.rowEl });
    pend.transcript.fire('touchend', { preventDefault() {} });
  };
  tapPend(); tapPend();
  assert.strictEqual(menuOn(pend.body), null);
});
