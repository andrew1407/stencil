// The transcript row menu (js/ui/view.js wireChatRowMenu): Copy / Insert on every settled
// row, Resend on a user row, and the "…" trigger's lift clear of the jump pills.
import { test } from 'node:test';
import assert from 'node:assert';
import { readFileSync } from 'node:fs';
import { stubDom } from '../../helpers/chatRowMenuRig.js';

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

test('the panel feeds the hovered row\'s trigger to the lift, and a pill hover can\'t hide it', () => {
  const panel = readFileSync(new URL('../../../js/ui/chat/panel.js', import.meta.url), 'utf8');
  const sync = panel.slice(panel.indexOf('const syncRowMenuLift = () => {'), panel.indexOf('const syncJumps = () => {'));
  assert.ok(sync.includes('rowMenuLiftPx(btn, pills)'), 'the pure test decides how far');
  assert.ok(sync.includes('rowMenuLiftFits(hoverRow.getBoundingClientRect(), btn, lift)'),
    'and the pure test decides whether it fits');
  assert.ok(sync.includes("setProperty('--row-menu-lift'"), 'a fit writes the CSS var the trigger reads');
  assert.ok(sync.includes("classList.add('chat-row-menu-yield')"), 'no fit hides it instead');
  // can-up/can-down answer to the popup-open reason only now — never the row overlap.
  const jumpsBody = panel.slice(panel.indexOf('const syncJumps = () => {'),
    panel.indexOf('transcript.addEventListener(\'scroll\', syncJumps'));
  assert.ok(/const up = [^\n]*!standDown;/.test(jumpsBody) && /const down = [^\n]*!standDown;/.test(jumpsBody));
  assert.ok(jumpsBody.includes("classList.toggle('can-up', up)")
    && jumpsBody.includes("classList.toggle('can-down', down)"));
  assert.ok(!jumpsBody.includes('rowMenuLiftPx') && !jumpsBody.includes('rowMenuHitsJumps'),
    'the pills no longer stand down for the row overlap reason');
  // Hover tracking is on the transcript, so a cursor on a pill (a sibling that floats
  // OVER it) leaves no row hovered and the pill survives.
  assert.ok(sync.includes(".chat-row-menu-btn'"), 'measured from the hovered row\'s own trigger');
  assert.ok(panel.includes("transcript.addEventListener('mouseover'"));
  assert.ok(panel.includes("transcript.addEventListener('mouseleave'"));
});

// A LIFTED trigger sits outside its row's own box, so reaching it crosses bare transcript first:
// only an actual different row, or a real mouseleave, may change what is hovered (user report).
test('reaching a lifted trigger never snaps it back: a no-row mouseover is ignored', () => {
  const panel = readFileSync(new URL('../../../js/ui/chat/panel.js', import.meta.url), 'utf8');
  const over = panel.slice(panel.indexOf("transcript.addEventListener('mouseover'"),
    panel.indexOf("transcript.addEventListener('mouseleave'"));
  assert.match(over, /if \(!row \|\| !transcript\.contains\(row\) \|\| row === hoverRow\) return;/,
    'no row (or the same one) is a no-op — it does NOT fall through to clearing hoverRow');
  // The clear only happens once we know we are switching to a REAL different row.
  const afterGuard = over.slice(over.indexOf('return;') + 'return;'.length);
  assert.ok(afterGuard.includes('clearRowMenuLift(hoverRow)'));
  assert.ok(afterGuard.includes('hoverRow = row;'), 'row is non-null past the guard — no `?? null` needed');
});
