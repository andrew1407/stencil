// The transcript row menu (js/ui/chat/view.js wireChatRowMenu): Copy / Insert on every settled
// row, Resend on a user row, and the "…" trigger's lift clear of the jump pills.
import { test } from 'node:test';
import assert from 'node:assert';
import { stubDom } from '../../helpers/chatRowMenuRig.js';
import { installChatDom, makeEl } from '../../helpers/chatSurfacesRig.js';

// ── Items per role ──
test('chatRowMenuItems: every settled row gets Copy / Insert; user rows add Resend', async () => {
  stubDom();
  const { chatRowMenuItems } = await import('../../../js/ui/chat/view.js?rowmenu-items');
  assert.deepStrictEqual(
    chatRowMenuItems({ role: 'assistant', text: 'hi' }).map((i) => i.label),
    ['Copy message', 'Insert into prompt'], 'assistant rows never offer Resend');
  assert.deepStrictEqual(
    chatRowMenuItems({ role: 'user', text: 'hi' }).map((i) => i.label),
    ['Copy message', 'Insert into prompt', 'Resend']);
  // An in-flight "…" row (and a missing record) gets no menu at all.
  assert.deepStrictEqual(chatRowMenuItems({ role: 'assistant', text: '…', pending: true }), []);
  assert.deepStrictEqual(chatRowMenuItems(null), []);
  // A FAILED turn is settled too — the unreachable card and a Stop both keep the menu
  // (they carry their own Retry beside it, they don't replace it).
  assert.deepStrictEqual(
    chatRowMenuItems({ role: 'assistant', text: 'Couldn\'t reach Ollama', error: true, card: true, retryText: 'x' })
      .map((i) => i.label),
    ['Copy message', 'Insert into prompt'], 'the error card keeps its menu');
  assert.deepStrictEqual(
    chatRowMenuItems({ role: 'assistant', text: 'Stopped.', error: true, retryText: 'x' }).map((i) => i.label),
    ['Copy message', 'Insert into prompt'], 'a stopped turn keeps its menu');
});

// The jump pills are the higher-priority control and stay put: a row's "…" lifts clear of them, or
// hides when the bubble is too short to lift it to (user report).
test('rowMenuLiftPx: the lift clears exactly the overlap, plus the gap', async () => {
  stubDom();
  const { rowMenuLiftPx } = await import('../../../js/ui/chat/view.js?rowmenu-jumps');
  // The measured collision from the live repro: the card's trigger under the ⌄ pill.
  const btn = { left: 296, right: 317, top: 181, bottom: 202, width: 21, height: 21 };
  const jumpTop = { left: 263, right: 291, top: 182, bottom: 210, width: 28, height: 28 };
  const jumpBottom = { left: 297, right: 325, top: 182, bottom: 210, width: 28, height: 28 };
  assert.strictEqual(rowMenuLiftPx(btn, [jumpTop, jumpBottom]), Math.ceil(202 - 182) + 6);
  // A user row's trigger sits far left of both — nothing to clear.
  const userBtn = { left: 153, right: 174, top: 181, bottom: 202, width: 21, height: 21 };
  assert.strictEqual(rowMenuLiftPx(userBtn, [jumpTop, jumpBottom]), 0);
  // …and so does a trigger well above the pills' band.
  assert.strictEqual(rowMenuLiftPx({ ...btn, top: 40, bottom: 61 }, [jumpTop, jumpBottom]), 0);
  // Nothing hovered / nothing shown / a collapsed rect: nothing to lift.
  assert.strictEqual(rowMenuLiftPx(null, [jumpBottom]), 0);
  assert.strictEqual(rowMenuLiftPx(btn, []), 0);
  assert.strictEqual(rowMenuLiftPx(btn, [{ left: 297, right: 297, top: 182, bottom: 182, width: 0, height: 0 }]), 0);
});

test('rowMenuLiftFits: only when the lifted trigger stays inside its own row', async () => {
  stubDom();
  const { rowMenuLiftFits } = await import('../../../js/ui/chat/view.js?rowmenu-jumps-fits');
  const row = { top: 100, bottom: 300 };
  const btn = { top: 260, bottom: 281 };
  assert.strictEqual(rowMenuLiftFits(row, btn, 40), true);    // 260-40=220, still >= 100
  assert.strictEqual(rowMenuLiftFits(row, btn, 200), false);  // 260-200=60, above the row's own top
  assert.strictEqual(rowMenuLiftFits(row, btn, 0), true);     // nothing to lift, always fits
  assert.strictEqual(rowMenuLiftFits(null, btn, 40), true);
  assert.strictEqual(rowMenuLiftFits(row, null, 40), true);
});

// The panel's jump pills (ui/chat/panel/jumpPills.js) wired over a live transcript: the
// ⌄ pill at the measured repro box, rows whose "…" sits under it.
const liftRig = async () => {
  const doc = installChatDom();
  const { wireJumpPills } = await import('../../../js/ui/chat/panel/jumpPills.js');
  const $ = (id) => doc.getElementById(id);
  const [transcript, jumps, top, bottom] = ['chat-transcript', 'chat-jumps', 'chat-jump-top', 'chat-jump-bottom'].map($);
  bottom.rect = { left: 297, right: 325, top: 182, bottom: 210, width: 28, height: 28 };
  const row = (rowTop) => {
    const r = makeEl();
    r.className = 'chat-msg';
    r.rect = { left: 150, right: 330, top: rowTop, bottom: 300, width: 180, height: 300 - rowTop };
    const btn = makeEl('button');
    btn.className = 'chat-row-menu-btn';
    btn.rect = { left: 296, right: 317, top: 181, bottom: 202, width: 21, height: 21 };
    r.append(btn);
    transcript.append(r);
    return r;
  };
  wireJumpPills({ transcript, jumps, jumpPills: [top, bottom] });
  const hover = (target) => transcript.fire('mouseover', { target });
  const lift = (r) => r.style['--row-menu-lift'];
  return { transcript, jumps, bottom, row, hover, lift };
};

test('the panel feeds the hovered row\'s trigger to the lift, and a pill hover can\'t hide it', async () => {
  const { transcript, jumps, row, hover, lift } = await liftRig();
  const { openComposerMenus, CHAT_POPUP_EVENT } = await import('../../../js/ui/chat/row/chatRowMenu.js');
  const { publish } = await import('../../../js/eventBus/appBus.js');
  const tall = row(100);
  const short = row(170);
  hover(tall.children[0]);
  assert.strictEqual(lift(tall), '26px', 'a fit writes the CSS var the trigger reads: the overlap plus the gap');
  assert.strictEqual(tall.classList.contains('chat-row-menu-yield'), false);
  hover(short);
  assert.strictEqual(lift(tall), undefined, 'leaving a row clears its lift');
  assert.strictEqual(lift(short), undefined, 'no room to lift…');
  assert.ok(short.classList.contains('chat-row-menu-yield'), '…so the trigger yields instead');
  // The pills answer to scroll position and the popup reason only, never the row overlap.
  Object.assign(transcript, { scrollTop: 50, scrollHeight: 500, clientHeight: 200 });
  transcript.fire('scroll');
  assert.ok(jumps.classList.contains('can-up') && jumps.classList.contains('can-down'),
    'an overlapping hovered row does not stand the pills down');
  openComposerMenus.add('probe');
  publish(CHAT_POPUP_EVENT);
  assert.ok(!jumps.classList.contains('can-up') && !jumps.classList.contains('can-down'), 'an open popup does');
  openComposerMenus.delete('probe');
  publish(CHAT_POPUP_EVENT);
  assert.ok(jumps.classList.contains('can-down'));
});

// A LIFTED trigger sits outside its row's own box, so reaching it crosses bare transcript first:
// only an actual different row, or a real mouseleave, may change what is hovered (user report).
test('reaching a lifted trigger never snaps it back: a no-row mouseover is ignored', async () => {
  const { transcript, bottom, row, hover, lift } = await liftRig();
  const first = row(100);
  const second = row(100);
  hover(first);
  assert.strictEqual(lift(first), '26px');
  hover(transcript);
  hover(bottom);
  assert.strictEqual(lift(first), '26px', 'bare background and a pill leave the hovered row lifted');
  hover(second.children[0]);
  assert.deepStrictEqual([lift(first), lift(second)], [undefined, '26px'], 'a real different row takes over');
  transcript.fire('mouseleave');
  assert.strictEqual(lift(second), undefined, 'a real mouseleave clears it');
});
