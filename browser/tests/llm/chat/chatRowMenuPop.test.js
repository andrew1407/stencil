// The entry pop and Resend's requeue (js/ui/chat/view.js): the menu grows out of the click
// point, refills an empty queue only, and both surfaces wire the one shared menu.
import { test } from 'node:test';
import assert from 'node:assert';
import { ANIMATIONS_CSS } from '../../helpers/css.js';
import { menuOn, wiredRow } from '../../helpers/chatRowMenuRig.js';
import { wireBothSurfaces, rowEl, rightClick, openRowMenu, clickRowItem } from '../../helpers/chatSurfacesRig.js';

// ── The entry pop: the menu grows out of the open point ──
test('menuPopOrigin: the click point relative to the placed box, clamped inside it', async () => {
  const { menuPopOrigin } = await import('../../../js/ui/motion.js');
  assert.strictEqual(menuPopOrigin(140, 90, { left: 100, top: 60, width: 120, height: 100 }), '40px 30px');
  // A box flipped left/up of the cursor pops from its far corner…
  assert.strictEqual(menuPopOrigin(300, 200, { left: 180, top: 100, width: 120, height: 100 }), '120px 100px');
  // …and a clamp that pushed the box past the click never yields a negative origin.
  assert.strictEqual(menuPopOrigin(2, 3, { left: 8, top: 8, width: 120, height: 100 }), '0px 0px');
});

test('the opened menu carries the click-point transform-origin, and CSS pops it from there', async () => {
  const { body, transcript, rowEl } = await wiredRow({ role: 'user', text: 'hi' }, {}, '-pop');
  // Near the bottom-right edge: the 120x100 stub menu flips left/up of the cursor.
  transcript.fire('contextmenu', { target: rowEl, clientX: 1020, clientY: 700,
    preventDefault() {}, stopPropagation() {} });
  const menu = menuOn(body);
  assert.strictEqual(menu.style.left, '900px');
  assert.strictEqual(menu.style.top, '600px');
  assert.strictEqual(menu.style.transformOrigin, '120px 100px',
    'the origin is the click inside the flipped box — the menu grows out of the cursor');
  // The animation itself is CSS: the shared menuPop keyframe, reduced-motion aware.
  const anims = ANIMATIONS_CSS;
  assert.match(anims, /@keyframes menuPop \{ from \{ opacity: 0; transform: scale\(0\.62\); \}/);
  assert.match(anims, /\.chat-row-menu \{ animation: menuPop 0\.14s ease-out; \}/);
  assert.match(anims, /prefers-reduced-motion: reduce\) \{\s*#ctx-menu\.ctx-open, \.chat-row-menu \{ animation: none; \}/);
});

// ── Resend's requeue mirrors requeueLastTurnAttachments ──
test('requeueRowAttachments refills an EMPTY queue only, capped, as analyze-images', async () => {
  const { requeueRowAttachments } = await import('../../../js/llm/chat/session.js');
  const { MAX_ATTACHMENTS } = await import('../../../js/llm/chat/controller.js');
  const rowAts = [
    { name: 'cat.jpg', kind: 'image', dataUrl: 'data:image/jpeg;base64,AAA' },
    { name: 'clip.mp4', kind: 'video', dataUrl: 'data:image/jpeg;base64,BBB' },   // first frame
    { name: 'broken.png', kind: 'image' },                                        // nothing renderable
  ];
  const ctrl = { attachments: [] };
  assert.strictEqual(requeueRowAttachments(ctrl, rowAts), 2);
  assert.deepStrictEqual(ctrl.attachments, [
    { name: 'cat.jpg', kind: 'image', use: 'analyze', dataUrl: 'data:image/jpeg;base64,AAA' },
    { name: 'clip.mp4', kind: 'image', use: 'analyze', dataUrl: 'data:image/jpeg;base64,BBB' },
  ], 'same shape the controller\'s own requeueLastTurnAttachments pushes');
  // Anything the user queued since WINS — resend must not mix batches.
  const busy = { attachments: [{ name: 'new.png' }] };
  assert.strictEqual(requeueRowAttachments(busy, rowAts), 0);
  assert.strictEqual(busy.attachments.length, 1);
  // And the §7 cap holds.
  const many = Array.from({ length: MAX_ATTACHMENTS + 2 },
    (_, i) => ({ name: `a${i}.png`, kind: 'image', dataUrl: 'data:image/png;base64,AA' }));
  const capped = { attachments: [] };
  assert.strictEqual(requeueRowAttachments(capped, many), MAX_ATTACHMENTS);
  assert.strictEqual(requeueRowAttachments(null, many), 0);
});

// ── Both surfaces wire it, and the chrome matches the app's other row menus ──
const tick = () => new Promise((r) => setTimeout(r, 0));

test('the panel and the flyout wire the SHARED row menu with insert + resend hooks', async () => {
  const s = await wireBothSurfaces();
  const row = s.session.appendChatRow({ role: 'user', text: 'crop it',
    attachments: [{ name: 'cat.png', kind: 'image', dataUrl: 'data:image/png;base64,AA' }] });
  for (const [name, surf] of [['panel', s.panel], ['flyout', s.flyout]]) {
    rightClick(surf.transcript, rowEl(surf.transcript, row.id));
    const menu = openRowMenu();
    assert.ok(menu, `${name} opens the shared row menu`);
    assert.deepStrictEqual(menu.children.map((b) => b.children[0].textContent),
      ['Copy message', 'Insert into prompt', 'Resend'], `${name}: a user row offers Resend`);
    surf.input.value = 'draft';
    clickRowItem(menu, 'Insert into prompt');
    assert.strictEqual(surf.input.value, 'draft\ncrop it', `${name} appends into its own composer`);
    assert.strictEqual(openRowMenu(), null, 'the menu closes on the pick');
  }
  for (const [name, surf] of [['panel', s.panel], ['flyout', s.flyout]]) {
    s.ctrl.attachments.length = 0;
    rightClick(surf.transcript, rowEl(surf.transcript, row.id));
    clickRowItem(openRowMenu(), 'Resend');
    assert.strictEqual(s.ctrl.sent.at(-1), 'crop it', `${name}'s Resend rides the composer's own send path`);
    assert.deepStrictEqual(s.ctrl.carried.at(-1), ['cat.png'], `${name} re-queued the row's attachments`);
    assert.strictEqual(s.session.chatTurnInFlight(), true, 'a logged turn is running');
    s.ctrl.settle({ reply: 'done', results: [] });
    await tick();
    assert.strictEqual(s.session.chatTurnInFlight(), false);
  }
});

test('the renderer stamps each row with its log record, and rewrites text only on a change', async () => {
  const s = await wireBothSurfaces();
  const row = s.session.appendChatRow({ role: 'assistant', text: '…', pending: true });
  const el = rowEl(s.panel.transcript, row.id);
  assert.strictEqual(el._chatRow, row, 'the menu reads the row the element was stamped with');
  const textEl = el.querySelector('.chat-msg-text');
  const own = Object.getOwnPropertyDescriptor(textEl, 'textContent');
  let writes = 0;
  Object.defineProperty(textEl, 'textContent', { get: own.get, set(v) { writes += 1; own.set.call(this, v); } });
  s.session.updateChatRow(row.id, { pending: false, text: '' });
  assert.strictEqual(writes, 1, 'settling out of the dots rewrites even an empty reply');
  assert.strictEqual(textEl.querySelector('.chat-typing'), null);
  s.session.updateChatRow(row.id, { text: 'Cropped.' });
  s.session.updateChatRow(row.id, {});
  s.session.updateChatRow(row.id, {});
  assert.strictEqual(writes, 2, 'an unchanged repaint leaves the text node alone');
  assert.strictEqual(textEl.textContent, 'Cropped.');
});

test('a row menu open over the flyout keeps the flyout engaged', async () => {
  const s = await wireBothSurfaces();
  const row = s.session.appendChatRow({ role: 'assistant', text: 'hi' });
  assert.strictEqual(s.flyout.el._keepOpen(), false, 'idle, unfocused: hover-out may close it');
  rightClick(s.flyout.transcript, rowEl(s.flyout.transcript, row.id));
  assert.ok(openRowMenu());
  assert.strictEqual(s.flyout.el._keepOpen(), true, 'the keep-open predicate consults the row menu');
});
