// Dropping onto the chat panel (js/ui/chat/panel/drop.js): a dragged page image arrives as a
// URL and is fetched into an attachment through the helper the canvas drop uses too, and a
// drop that misses the composer is swallowed by the panel, never handed to the canvas.
import { test } from 'node:test';
import assert from 'node:assert';
import { wireBothSurfaces } from '../../helpers/chatSurfacesRig.js';

const tick = () => new Promise((r) => setTimeout(r, 0));

// An image dragged from another PAGE carries no File — only a URL in uri-list/html.
// The composer used to read Files alone, so such a drop silently attached nothing.
const dt = (data = {}, files = []) => ({ files, items: [], types: Object.keys(data),
  getData: (t) => data[t] || '', dropEffect: 'none' });
const settle = async () => { for (let i = 0; i < 8; i++) await tick(); };
const reason = (text) => text.slice(text.indexOf(' — ') + 3, text.indexOf('. If the site'));
const png = async () => ({ ok: true, blob: async () => new Blob(['x'], { type: 'image/png' }) });

test('a dropped image URL is fetched into an attachment, not silently dropped', async () => {
  const s = await wireBothSurfaces();
  s.app.chat.open();
  const row = s.doc.getElementById('chat-input-wrap');
  const fetched = [];
  let answer = png;
  const saved = globalThis.fetch;
  const savedReader = globalThis.FileReader;
  globalThis.fetch = async (url) => { fetched.push(url); return answer(); };
  try {
    const url = { 'text/uri-list': 'https://h.example/cat.png' };
    row.fire('drop', { dataTransfer: dt(url, [{ name: 'own.png', type: 'image/png' }]) });
    await settle();
    assert.deepStrictEqual([fetched.length, s.ctrl.attachments.map((a) => a.name)], [0, ['own.png']], 'Files still win');
    s.ctrl.attachments.length = 0;
    row.fire('drop', { dataTransfer: dt(url) });
    await settle();
    assert.deepStrictEqual([fetched[0], s.ctrl.attachments.map((a) => a.name)], [url['text/uri-list'], ['cat.png']]);
    // A failure is reported, never swallowed — that silence was the whole bug.
    answer = async () => { throw new Error('offline'); };
    row.fire('drop', { dataTransfer: dt(url) });
    await settle();
    const chatFail = s.notices.at(-1);
    assert.ok(chatFail.text.startsWith("Couldn't attach that image — ") && chatFail.type === 'fail', chatFail.text);
    row.fire('drop', { dataTransfer: dt() });
    assert.deepStrictEqual([s.notices.at(-1).text, s.notices.at(-1).type], ['Nothing to attach from that drop', 'fail']);
    // The canvas drop reads and fetches the same way: one helper, one name, one reason.
    // The real open path (core/launch/openFlow.js) as far as the browser's file reader, which
    // records the File it is handed.
    const opened = [];
    globalThis.FileReader = class { readAsDataURL(file) { opened.push(file.name); } };
    const { wireDropPaste } = await import('../../../js/ui/bindings/dropPaste.js');
    wireDropPaste({
      storage: { incognito: false, temporary: true, save() {}, newTemporary() {}, promoteTemporaryToProject() {} },
      zoomPan: { syncViewportHeight() {} },
      tabs: { reportActive() {}, reportIncognito() {} },
    });
    answer = png;
    s.doc.fire('drop', { dataTransfer: dt({ 'text/html': '<img src="https://h.example/cat.png">' }), clientX: 5, clientY: 5 });
    await settle();
    assert.deepStrictEqual(opened, ['cat.png'], 'the canvas fetched the dragged URL into the same File');
    answer = async () => { throw new Error('offline'); };
    s.doc.fire('drop', { dataTransfer: dt(url), clientX: 5, clientY: 5 });
    await settle();
    assert.strictEqual(reason(s.notices.at(-1).text), reason(chatFail.text), 'the same helper names the failure');
  } finally { globalThis.fetch = saved; globalThis.FileReader = savedReader; }
});

test('the composer ACTS on a drop; the panel SWALLOWS one (never the canvas)', async () => {
  const s = await wireBothSurfaces();
  const host = s.panel.host;
  const row = s.doc.getElementById('chat-input-wrap');
  const image = () => dt({}, [{ name: 'a.png', type: 'image/png' }]);
  assert.strictEqual(host.fire('drop', { dataTransfer: image() }).prevented, undefined, 'a closed panel takes nothing');
  s.app.chat.open();
  // The whole panel owns drops while open, so the canvas never lights its zones under it.
  assert.ok(host.hasAttribute('data-drop-owner'));
  const over = row.fire('dragover', { dataTransfer: image() });
  assert.ok(over.prevented && row.classList.contains('chat-drop-target'), 'the composer is the target');
  row.fire('dragleave', { relatedTarget: null });
  assert.strictEqual(row.classList.contains('chat-drop-target'), false);
  // A drop that MISSES the composer never falls through to the canvas, and says where to aim.
  const miss = host.fire('drop', { dataTransfer: image() });
  assert.ok(miss.prevented && miss.stopped, 'the panel takes the leftovers');
  assert.deepStrictEqual([s.notices.at(-1).text, s.notices.at(-1).type], ['Drop it on the message box to attach it', 'info']);
  assert.strictEqual(s.ctrl.attachments.length, 0, 'and attaches nothing');
});
