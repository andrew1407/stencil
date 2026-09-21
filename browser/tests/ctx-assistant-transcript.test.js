// One rendered transcript for both surfaces (js/llm/session.js): the append/update/clear
// row list, the shared logged-turn frame, dropped image URLs and the scatter mesh budget.
import { test } from 'node:test';
import assert from 'node:assert';
import { readFileSync } from 'node:fs';
import {
  chatLog, onChatLog, appendChatRow, updateChatRow, clearChatLog, resetChatLog,
  clearSharedConversation, runLoggedChatTurn, attachmentPreviews,
} from '../js/llm/chat/session.js';
import { fileNameForUrl } from '../js/core/pointer/dragImageUrl.js';
import { scatterGridFor, SCATTER_TILE_BUDGET, SCATTER_MAX_ROWS } from '../js/ui/motion.js';
import { COMPONENTS_CSS } from './helpers/css.js';
import { chatViewSource } from './helpers/chatViewSource.js';
import { contextMenuSource } from './helpers/contextMenuSource.js';

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

test('a surface renders the history that already exists (not only live appends)', () => {
  resetChatLog();
  // A menu-only conversation…
  appendChatRow({ role: 'user', text: 'from the menu' });
  appendChatRow({ role: 'assistant', text: 'Done.' });
  // …and the panel wires up later: it paints from chatLog(), so it is never empty.
  const painted = [];
  onChatLog((rows) => painted.push(rows.length));
  const atWireTime = chatLog().map((r) => r.text);
  assert.deepStrictEqual(atWireTime, ['from the menu', 'Done.'], 'the log carries the backlog');
  const src = readFileSync(new URL('../js/ui/chat/panel.js', import.meta.url), 'utf8');
  assert.ok(src.includes('paint();   // renders whatever the conversation already holds'),
    'the panel paints once at wire time, not only on change');
  const ctx = contextMenuSource();
  assert.ok(ctx.includes('onChatLog(paint);') && ctx.includes('\n  paint();'), 'and so does the flyout');
  resetChatLog();
  assert.strictEqual(painted.length, 0, 'no spurious notifications from reading');
});

test('both send loops run the SAME shared logged-turn frame', () => {
  const panel = readFileSync(new URL('../js/ui/chat/panel.js', import.meta.url), 'utf8');
  const menu = contextMenuSource();
  for (const [name, src] of [['panel', panel], ['flyout', menu]]) {
    assert.ok(src.includes('await runLoggedChatTurn('), `${name} runs the shared logged-turn frame`);
    assert.ok(src.includes('renderChatLog('), `${name} renders the log, never its own private DOM`);
  }
  // The frame itself (session.js) owns the row writes: user turn, pending "…"
  // reply, and the in-place ok/error patch — so the surfaces cannot drift.
  const session = readFileSync(new URL('../js/llm/chat/session.js', import.meta.url), 'utf8');
  assert.ok(session.includes("appendChatRow({ role: 'user', text, attachments: attachmentPreviews(controller) });"),
    'the frame logs the user turn, carrying the images the user attached to it');
  assert.ok(session.includes("const pending = appendChatRow({ role: 'assistant', text: '…', pending: true });"),
    'the frame logs a pending reply, marked so the view can animate it');
  assert.ok(session.includes('updateChatRow(pending.id'), 'the frame resolves that row in place');
  // The panel's Clear runs the shared clear path (which repaints the flyout too).
  assert.ok(panel.includes('clearSharedConversation(app);'));
});

// An image dragged from another PAGE carries no File — only a URL in uri-list/html.
// The composer used to read Files alone, so such a drop silently attached nothing.
test('a dropped image URL is fetched into an attachment, not silently dropped', () => {
  const panel = readFileSync(new URL('../js/ui/chat/panel.js', import.meta.url), 'utf8');
  assert.ok(panel.includes('const files = mediaFilesFromData(e.dataTransfer);'), 'Files still win');
  assert.ok(panel.includes('const url = extractDraggedImageUrl((t) => e.dataTransfer.getData(t));'),
    'and a File-less drag falls back to its URL');
  assert.match(panel, /await attachFiles\(\[await fetchDraggedMediaFile\(url, \{ accept: \/\^\(image\|video\)\\\/\/ \}\)\]\);/);
  // A failure is reported, never swallowed — that silence was the whole bug.
  assert.ok(panel.includes("notify(`Couldn't attach that image — ${err.message}`, 'fail');"));
  assert.ok(panel.includes("notify('Nothing to attach from that drop', 'fail');"));
  // The fetch itself is the canvas's, shared rather than re-implemented.
  const drag = readFileSync(new URL('../js/core/pointer/dragImageUrl.js', import.meta.url), 'utf8');
  assert.match(drag, /export const fetchDraggedMediaFile = async \(url, \{ accept = \/\^image\\\/\/ \} = \{\}\)/);
  const binder = readFileSync(new URL('../js/ui/bindings/dropPaste.js', import.meta.url), 'utf8');
  assert.ok(binder.includes('const fetchUrlToFile = (url) => fetchDraggedMediaFile(url);'),
    'the canvas drop uses the same helper (no second copy)');
});

test('the composer ACTS on a drop; the panel SWALLOWS one (never the canvas)', () => {
  const panel = readFileSync(new URL('../js/ui/chat/panel.js', import.meta.url), 'utf8');
  // Attaching is the composer's.
  assert.ok(panel.includes("const dropRow = $('chat-input-wrap');"));
  for (const ev of ['dragover', 'dragleave', 'drop']) {
    assert.ok(panel.includes(`dropRow.addEventListener('${ev}'`), `${ev} is wired on the composer`);
  }
  // A drop that MISSES the chat must not fall through to the canvas: the editor's
  // replace-or-new-page dialog is never what dropping onto a chat means.
  assert.ok(panel.includes("host.addEventListener('drop'"), 'the panel takes the leftovers');
  assert.ok(panel.includes("notify('Drop it on the message box to attach it', 'info');"),
    'and says where to aim instead of silently eating it');
  // The whole panel owns drops, so the canvas never lights its zones under an open chat.
  assert.ok(panel.includes("host.toggleAttribute('data-drop-owner', on);"));
});

test('the attachment chip is a thumbnail, a name and a remove — nothing else', () => {
  const view = chatViewSource();
  assert.ok(view.includes("thumb.className = 'chat-attach-thumb';"), 'the queued picture is shown');
  assert.ok(view.includes('wireThumbPreview(thumb, label);'), 'and magnifies on hover');
  assert.ok(view.includes('name.dataset.title = label;'), 'the ellipsised name keeps the full one on the tooltip');
  assert.ok(view.includes('chip.append(name, rm);'), 'name + remove, no analyze/working pill');
  assert.ok(!view.includes('chat-attach-use'), 'the analyze ↔ working toggle is gone from the chip');
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

test('hovering a small attachment thumbnail shows it large', () => {
  const view = chatViewSource();
  assert.ok(view.includes('export const wireThumbPreview = (img, caption = '), 'the preview helper exists');
  assert.ok(view.includes("wireThumbPreview(img, a.kind === 'video' ? `${a.name} (first frame)` : a.name);"),
    'and every transcript thumbnail is wired to it');
  assert.ok(view.includes("cap.textContent = caption;"), 'the filename is text, never markup');
  // On the BODY: the panel clips its overflow, so an in-place popup would be cut off.
  assert.ok(view.includes('document.body.appendChild(box);'));
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
