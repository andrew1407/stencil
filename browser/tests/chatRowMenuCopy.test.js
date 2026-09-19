// Copying and opening (js/ui/chatView.js): the exact text to the clipboard with an
// execCommand fallback, the native menu kept where it belongs, and the surface hooks.
import { test } from 'node:test';
import assert from 'node:assert';
import { docListeners, stubDom, menuOn, itemLabels, clickItem, wiredRow } from './helpers/chatRowMenuRig.js';

// ── Copy ──
test('copyChatText hands the EXACT text to the async clipboard API', async () => {
  stubDom();
  const { copyChatText } = await import('../js/ui/chatView.js?rowmenu-copy');
  const writes = [];
  const nav = { clipboard: { writeText: async (t) => writes.push(t) } };
  const text = 'multi\nline — reply © exact';
  assert.strictEqual(await copyChatText(text, globalThis.document, nav), true);
  assert.deepStrictEqual(writes, [text]);
});

test('copyChatText falls back to execCommand, and reports failure instead of lying', async () => {
  const { body } = stubDom();
  const { copyChatText } = await import('../js/ui/chatView.js?rowmenu-copy2');
  // No async API at all → the hidden-textarea path, with the exact text in it.
  const cmds = [];
  globalThis.document.execCommand = (c) => { cmds.push({ c, v: body.children.at(-1)?.value }); return true; };
  assert.strictEqual(await copyChatText('fallback text', globalThis.document, {}), true);
  assert.deepStrictEqual(cmds, [{ c: 'copy', v: 'fallback text' }]);
  assert.strictEqual(body.children.length, 0, 'the scratch textarea is removed again');
  // A rejecting writeText ALSO falls through to execCommand…
  cmds.length = 0;
  const nav = { clipboard: { writeText: async () => { throw new Error('denied'); } } };
  assert.strictEqual(await copyChatText('second try', globalThis.document, nav), true);
  assert.strictEqual(cmds[0].v, 'second try');
  // …and when even that refuses, the caller hears `false` (that is what toasts).
  globalThis.document.execCommand = () => false;
  assert.strictEqual(await copyChatText('nope', globalThis.document, {}), false);
});

// ── Opening / gating ──
test('right-click on a settled row opens the menu; Copy message copies that row\'s text', async () => {
  const row = { role: 'assistant', text: 'the exact reply' };
  const { body, rightClick } = await wiredRow(row, {}, '-open');
  // Node's own `navigator` has no clipboard, so the menu's copy takes the
  // execCommand fallback — record what lands in the scratch textarea.
  const writes = [];
  globalThis.document.execCommand = () => { writes.push(body.children.at(-1)?.value); return true; };
  const ev = rightClick();
  assert.strictEqual(ev.prevented, true, 'the native menu is replaced');
  const menu = menuOn(body);
  assert.ok(menu, 'the menu floats on the body');
  assert.deepStrictEqual(itemLabels(menu), ['Copy message', 'Insert into prompt']);
  clickItem(menu, 'Copy message');
  await new Promise((r) => setTimeout(r, 0));
  assert.deepStrictEqual(writes, ['the exact reply'], 'the copy callback got the exact text');
  assert.strictEqual(menuOn(body), null, 'an item click closes the menu');
});

test('a pending row, and a right-click on an existing selection, keep the native menu', async () => {
  const { body, rightClick } = await wiredRow({ role: 'assistant', text: '…', pending: true }, {}, '-gate');
  assert.strictEqual(rightClick().prevented, false, 'pending rows offer nothing to act on');
  assert.strictEqual(menuOn(body), null);
  // An existing selection over the row: the NATIVE menu's Copy acts on exactly it.
  const sel = await wiredRow({ role: 'assistant', text: 'done' }, {}, '-gate2');
  globalThis.window.getSelection = () => ({
    isCollapsed: false, toString: () => 'part of it', containsNode: () => true,
  });
  assert.strictEqual(sel.rightClick().prevented, false, 'the selection keeps the native menu');
  assert.strictEqual(menuOn(sel.body), null);
});

// ── The surface hooks ──
test('Insert into prompt hands the row text to the surface\'s composer hook', async () => {
  const inserted = [];
  const row = { role: 'user', text: 'crop 10% off every edge' };
  const { body, rightClick } = await wiredRow(row, { onInsert: (t) => inserted.push(t) }, '-insert');
  rightClick();
  clickItem(menuOn(body), 'Insert into prompt');
  assert.deepStrictEqual(inserted, ['crop 10% off every edge']);
});

test('Resend hands the text AND the row\'s original attachments to the hook', async () => {
  const sent = [];
  const attachments = [{ name: 'cat.jpg', kind: 'image', dataUrl: 'data:image/jpeg;base64,AAA' }];
  const row = { role: 'user', text: 'what is this?', attachments };
  const { body, rightClick } = await wiredRow(row, { onResend: (t, a) => sent.push([t, a]) }, '-resend');
  rightClick();
  const menu = menuOn(body);
  assert.ok(itemLabels(menu).includes('Resend'), 'user rows carry Resend');
  clickItem(menu, 'Resend');
  assert.deepStrictEqual(sent, [['what is this?', attachments]]);
});

test('the menu closes on Escape (and arms outside-press + scroll closers)', async () => {
  const { body, rightClick } = await wiredRow({ role: 'user', text: 'hi' }, {}, '-close');
  rightClick();
  assert.ok(menuOn(body));
  await new Promise((r) => setTimeout(r, 5));   // the closers arm a tick late
  const key = docListeners.find((l) => l.t === 'keydown' && l.cap);
  assert.ok(key, 'Escape closes via a capture listener (it must beat the ctx-menu\'s own)');
  const stops = [];
  key.fn({ key: 'x', stopPropagation: () => stops.push('x') });
  assert.ok(menuOn(body), 'other keys leave it open');
  assert.ok(docListeners.some((l) => l.t === 'pointerdown' && l.cap), 'outside press closes it');
  key.fn({ key: 'Escape', stopPropagation: () => stops.push('esc') });
  assert.strictEqual(menuOn(body), null, 'Escape closes it');
  assert.deepStrictEqual(stops, ['esc'], 'and only Escape is swallowed');
  // …and close() unhooks its document closers — nothing leaks past the menu.
  assert.ok(!docListeners.some((l) => l.t === 'keydown'), 'the keydown closer is removed with the menu');
  assert.ok(!docListeners.some((l) => l.t === 'pointerdown'), 'so is the outside-press closer');
});
