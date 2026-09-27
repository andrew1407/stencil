// One rendered transcript for both surfaces (js/llm/chat/session.js): the append/update/clear
// row list, the shared logged-turn frame, the attachment chip and its hover preview, and the
// scatter mesh budget.
import { test } from 'node:test';
import assert from 'node:assert';
import {
  chatLog, onChatLog, appendChatRow, updateChatRow, clearChatLog, resetChatLog,
} from '../../../js/llm/chat/session.js';
import { fileNameForUrl } from '../../../js/core/pointer/dragImageUrl.js';
import { scatterGridFor, SCATTER_TILE_BUDGET, SCATTER_MAX_ROWS } from '../../../js/ui/motion.js';
import { COMPONENTS_CSS } from '../../helpers/css.js';
import { wireBothSurfaces, typeAndSend, rowEl, installChatDom } from '../../helpers/chatSurfacesRig.js';

const tick = () => new Promise((r) => setTimeout(r, 0));
const rowTexts = (t) => t.children.filter((c) => c.classList.contains('chat-msg'))
  .map((c) => c.querySelector('.chat-msg-text').textContent);

// ── One rendered transcript for both surfaces ──
test('the chat log is append/update/clear with subscribers — one row list, one order', () => {
  resetChatLog();
  const seen = [];
  const off = onChatLog((rows) => seen.push(rows.map((r) => `${r.role}:${r.text}`).join(' | ')));
  const user = appendChatRow({ role: 'user', text: 'make it sepia' });
  const pending = appendChatRow({ role: 'assistant', text: '…' });
  assert.notStrictEqual(user.id, pending.id, 'rows are keyed, so renderers can update in place');
  assert.deepStrictEqual(chatLog().map((r) => r.text), ['make it sepia', '…']);
  // The pending row becomes the reply — same id, so nothing re-renders from scratch.
  updateChatRow(pending.id, { text: 'Applied sepia.', results: [{ label: 'rotated', dataUrl: 'data:,' }] });
  assert.strictEqual(chatLog()[1].text, 'Applied sepia.');
  assert.strictEqual(chatLog()[1].results.length, 1);
  assert.strictEqual(chatLog().length, 2, 'updating never appends a duplicate');
  // Errors are flags on the row (both surfaces style them the same way).
  updateChatRow(pending.id, { text: 'Stopped.', error: true, results: undefined });
  assert.deepStrictEqual(
    { text: chatLog()[1].text, error: chatLog()[1].error },
    { text: 'Stopped.', error: true });
  // Every mutation notified every subscriber, in order.
  assert.deepStrictEqual(seen, [
    'user:make it sepia',
    'user:make it sepia | assistant:…',
    'user:make it sepia | assistant:Applied sepia.',
    'user:make it sepia | assistant:Stopped.',
  ]);
  clearChatLog();
  assert.deepStrictEqual(chatLog(), [], 'Clear empties it for everyone');
  assert.strictEqual(seen.at(-1), '');
  off();
  appendChatRow({ role: 'user', text: 'after unsubscribe' });
  assert.strictEqual(seen.length, 5, 'unsubscribed listeners stop hearing');
  resetChatLog();
});

test('a surface renders the history that already exists (not only live appends)', async () => {
  resetChatLog();
  // A menu-only conversation…
  appendChatRow({ role: 'user', text: 'from the menu' });
  appendChatRow({ role: 'assistant', text: 'Done.' });
  // …and the panel wires up later: it paints from chatLog(), so it is never empty.
  const painted = [];
  onChatLog((rows) => painted.push(rows.length));
  const atWireTime = chatLog().map((r) => r.text);
  assert.deepStrictEqual(atWireTime, ['from the menu', 'Done.'], 'the log carries the backlog');
  resetChatLog();
  assert.strictEqual(painted.length, 0, 'no spurious notifications from reading');
  // Both surfaces paint once at wire time, not only on change.
  const s = await wireBothSurfaces({ backlog: [{ role: 'user', text: 'from the menu' }, { role: 'assistant', text: 'Done.' }] });
  for (const [name, surf] of [['panel', s.panel], ['flyout', s.flyout]]) {
    assert.deepStrictEqual(rowTexts(surf.transcript), ['from the menu', 'Done.'], `${name} paints the backlog`);
  }
  s.session.appendChatRow({ role: 'user', text: 'live' });
  for (const surf of [s.panel, s.flyout]) assert.strictEqual(rowTexts(surf.transcript).at(-1), 'live');
});

test('both send loops run the SAME shared logged-turn frame', async () => {
  const s = await wireBothSurfaces();
  for (const [name, surf] of [['panel', s.panel], ['flyout', s.flyout]]) {
    s.ctrl.attachments.push({ name: 'cat.png', kind: 'image', dataUrl: 'data:image/png;base64,AA' });
    typeAndSend(surf, `from the ${name}`);
    const [user, reply] = s.session.chatLog().slice(-2);
    // The frame logs the user turn carrying its images, then a pending reply…
    assert.deepStrictEqual([user.role, user.text, user.attachments.map((a) => a.name)],
      ['user', `from the ${name}`, ['cat.png']], `${name} logs the user turn with its attachments`);
    assert.strictEqual(reply.pending, true);
    for (const t of [s.panel.transcript, s.flyout.transcript]) {
      assert.ok(rowEl(t, reply.id).querySelector('.chat-typing'), 'rendered from the log on both surfaces');
    }
    // …and resolves that same row in place.
    s.ctrl.settle({ reply: 'Done.', results: [] });
    await tick();
    const last = s.session.chatLog().at(-1);
    assert.deepStrictEqual([last.id, last.pending, last.text], [reply.id, false, 'Done.']);
    s.ctrl.attachments.length = 0;
  }
  // The panel's Clear runs the shared clear path, which repaints the flyout too.
  s.doc.getElementById('chat-clear').fire('click');
  assert.deepStrictEqual([s.ctrl.cleared, s.session.chatLog().length], [1, 0]);
  assert.strictEqual(s.flyout.transcript.querySelectorAll('[data-row]').length, 0, 'the flyout rows leave too');
});

test('the attachment chip is a thumbnail, a name and a remove — nothing else', async () => {
  const doc = installChatDom();
  const { chatAttachmentChips } = await import('../../../js/ui/chat/view.js');
  const list = doc.createElement('div');
  const ctrl = { attachments: [{ name: 'cat.png', kind: 'image', dataUrl: 'data:image/png;base64,AA' },
    { name: 'clip.mp4', kind: 'video', frames: ['data:image/jpeg;base64,BB', 'data:image/jpeg;base64,CC'] }] };
  chatAttachmentChips(list, ctrl);
  const [cat, clip] = list.children;
  assert.deepStrictEqual(cat.children.map((c) => c.className),
    ['chat-attach-thumb', 'chat-attach-name', 'chat-hbtn chat-attach-remove'], 'no analyze/working pill');
  assert.strictEqual(cat.children[0].src, 'data:image/png;base64,AA', 'the queued picture is shown');
  const name = clip.children[1];
  assert.deepStrictEqual([name.textContent, name.dataset.title], ['clip.mp4 (2 frames)', 'clip.mp4 (2 frames)'],
    'the ellipsised name keeps the full one on the tooltip');
  cat.children[0].fire('mouseenter');
  assert.ok(doc.body.querySelector('.chat-thumb-preview'), 'and magnifies on hover');
  const css = COMPONENTS_CSS;
  assert.match(css, /\.chat-attach-thumb \{[^}]*object-fit: cover/);
  assert.match(css, /\.chat-attach-name \{[^}]*text-overflow: ellipsis/);
});

test('a fetched data: URL gets a readable filename, not its base64 payload', () => {
  // The chip showed "bXNxIKcJ5cNNW8QFr…" — the whole payload, read as a path segment.
  assert.strictEqual(fileNameForUrl('data:image/png;base64,iVBORw0KGgoAAA', 'image/png'), 'image.png');
  assert.strictEqual(fileNameForUrl('blob:http://x/9f2-ab', 'image/jpeg'), 'image.jpg');
  assert.strictEqual(fileNameForUrl('http://h/a/cat.png?v=2', 'image/png'), 'cat.png');
  assert.strictEqual(fileNameForUrl('http://h/photos/', 'image/webp'), 'image.webp');
  assert.strictEqual(fileNameForUrl('http://h/x', ''), 'x');   // a real segment is a real name
  // …but an opaque id longer than a filename is not one.
  assert.strictEqual(fileNameForUrl(`http://h/${'a'.repeat(120)}`, 'image/png'), 'image.png');
  // An extensionless segment with a KNOWN MIME is an endpoint, not a filename —
  // a thumbnail CDN's /images?q=… named every chip "images".
  assert.strictEqual(
    fileNameForUrl('https://thumbs.example.com/images?q=tbn:ANd9GcT&s=10', 'image/jpeg'),
    'image.jpg');
});

test('hovering a small attachment thumbnail shows it large', async () => {
  const doc = installChatDom();
  const { chatAttachmentStrip } = await import('../../../js/ui/chat/view.js');
  const strip = chatAttachmentStrip([{ name: '<b>cat</b>.png', kind: 'image', dataUrl: 'data:image/png;base64,AA' },
    { name: 'clip.mp4', kind: 'video', dataUrl: 'data:image/jpeg;base64,BB' }]);
  const [cat, clip] = strip.children.map((fig) => fig.children[0]);
  const shown = () => doc.body.children.filter((c) => c.classList.contains('chat-thumb-preview'));
  cat.fire('mouseenter');
  // On the BODY: the panel clips its overflow, so an in-place popup would be cut off.
  const [box] = shown();
  assert.ok(box, 'every transcript thumbnail is wired to the preview');
  assert.strictEqual(box.children[0].src, 'data:image/png;base64,AA', 'the same picture, large');
  const cap = box.children[1];
  assert.deepStrictEqual([cap.textContent, cap.innerHTML], ['<b>cat</b>.png', ''], 'the filename is text, never markup');
  clip.fire('mouseenter');
  assert.deepStrictEqual(shown().map((b) => b.children[1].textContent), ['clip.mp4 (first frame)'], 'one preview at a time');
  clip.fire('mouseleave');
  assert.strictEqual(shown().length, 0);
  const css = COMPONENTS_CSS;
  assert.match(css, /\.chat-thumb-preview \{[^}]*position: fixed;/);
  assert.match(css, /\.chat-thumb-preview \{[^}]*pointer-events: none;/, 'it must not steal its own hover');
  // A GLANCE, not a lightbox: the same clamp the projects modal's row zoom uses.
  assert.match(css, /\.chat-thumb-preview img \{[^}]*max-width: 25vw;[^}]*max-height: 20vh;/);
  const zoom = /\.project-thumb-zoom img \{([^}]*)\}/.exec(css);
  assert.ok(zoom && /max-width: 25vw/.test(zoom[1]) && /max-height: 20vh/.test(zoom[1]),
    'the two hover previews stay clamped by the same rule');
});

test('the scatter mesh is budgeted by how many rows leave at once', () => {
  const one = scatterGridFor(1);
  // A single removal keeps the finest grain the budget allows…
  assert.ok(one.cols * one.rows <= SCATTER_TILE_BUDGET);
  assert.ok(one.cols >= 24 && one.rows >= 12, 'a lone row still comes apart as dust');
  // …and a whole-transcript wipe coarsens until the TOTAL fits (2,028 blurred clones
  // at once is what made the extension's popup crawl).
  for (const n of [2, 6, 12, 20, 200]) {
    const g = scatterGridFor(n);
    const flying = Math.min(n, SCATTER_MAX_ROWS) * g.cols * g.rows;
    assert.ok(flying <= SCATTER_TILE_BUDGET * 1.05, `${n} rows → ${flying} tiles is over budget`);
    assert.ok(g.cols >= 8 && g.rows >= 4, 'never so coarse it reads as broken glass');
    assert.ok(g.cols <= 32 && g.rows <= 16);
  }
  // Past the cap the extra rows fade with no dust at all — a dozen simultaneous
  // scatters is already more than the eye resolves.
  assert.deepStrictEqual(scatterGridFor(20, SCATTER_MAX_ROWS), { cols: 0, rows: 0 });
  assert.ok(scatterGridFor(20, SCATTER_MAX_ROWS - 1).cols > 0);
  // Monotonic: more rows never means a finer per-row mesh.
  assert.ok(scatterGridFor(8).cols <= scatterGridFor(4).cols);
});
