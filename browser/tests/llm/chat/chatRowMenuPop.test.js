// The entry pop and Resend's requeue (js/ui/view.js): the menu grows out of the click
// point, refills an empty queue only, and both surfaces wire the one shared menu.
import { test } from 'node:test';
import assert from 'node:assert';
import { readFileSync } from 'node:fs';
import { ANIMATIONS_CSS } from '../../helpers/css.js';
import { chatViewSource } from '../../helpers/chatViewSource.js';
import { contextMenuSource } from '../../helpers/contextMenuSource.js';
import { menuOn, wiredRow } from '../../helpers/chatRowMenuRig.js';

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
test('the panel and the flyout wire the SHARED row menu with insert + resend hooks', () => {
  const panel = readFileSync(new URL('../../../js/ui/chat/panel.js', import.meta.url), 'utf8');
  const menu = contextMenuSource();
  for (const [name, src] of [['panel', panel], ['flyout', menu]]) {
    assert.ok(src.includes('wireChatRowMenu(transcript, {'), `${name} wires the shared menu`);
    assert.ok(src.includes('onInsert: (text) => {'), `${name} passes its composer hook`);
    assert.ok(src.includes('requeueRowAttachments('), `${name}'s Resend re-queues the row attachments`);
    assert.ok(src.includes('runTurn(text).catch('), `${name}'s Resend rides the composer's own send path`);
  }
  // The renderer stamps rows with their log record — that is what the menu reads.
  const view = chatViewSource();
  assert.ok(view.includes('el._chatRow = row;'));
  // A repaint compares the row's OWN text node, not the whole bubble, and replaces only on a change —
  // or when the row settles out of its typing dots, which read as '' and match an empty reply.
  assert.ok(view.includes('} else if (typing || textEl.textContent !== row.text) {'),
    'text nodes are replaced only when the text changed, dots aside');
  // A row menu open over the flyout counts as "engaged" — hover-out must not close it.
  assert.ok(menu.includes('chatRowMenuOpen()'), 'the flyout keep-open predicate consults it');
});
