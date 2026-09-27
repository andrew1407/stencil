// Both drop paths driven with a dragged URL whose fetch fails: each surfaces the module's
// message (which names the host and the count) and carries the same fallback hint.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { installDom, createStubElement } from '../../helpers/dom.js';
import { wireDropPaste } from '../../../js/ui/bindings/dropPaste.js';
import { wirePanelDrop } from '../../../js/ui/chat/panel/drop.js';

const HINT = 'If the site blocks cross-origin downloads, try the extension or desktop app.';
const CORS = '(CORS or an unreachable host)';

// A drag from another page: a URL, no File; every fetch refused like a CORS block.
const refusedUrlDrag = () => {
  const toasts = [];
  const doc = installDom({}, {
    window: { innerWidth: 1000, innerHeight: 800 },
    fetch: async () => { throw new TypeError('Failed to fetch'); },
  });
  doc.register('notify-balloon', createStubElement('div', { notify: (msg, type) => toasts.push({ msg, type }) }));
  const dataTransfer = {
    types: ['text/uri-list'], files: [],
    getData: (t) => (t === 'text/uri-list' ? 'https://blocked.example/a.png' : ''),
  };
  return { doc, toasts, dataTransfer };
};
const settle = () => new Promise((r) => setTimeout(r, 0));

test('the drop notice surfaces the fetch error rather than blaming CORS itself', async () => {
  const { doc, toasts, dataTransfer } = refusedUrlDrag();
  try {
    doc.register('global-drop-overlay', createStubElement('div'));
    wireDropPaste({ image: null });
    doc.dispatch('drop', { preventDefault() {}, clientX: 100, clientY: 100, dataTransfer });
    await settle();
    assert.equal(toasts.length, 1);
    const { msg, type } = toasts[0];
    assert.equal(type, 'fail');
    assert.ok(msg.startsWith('Could not load the dragged image — the only URL in that drag failed; blocked.example refused'),
      msg);
    assert.ok(msg.endsWith(HINT), 'the extension/desktop hint stays');
    assert.equal(msg.split(CORS).length, 2,
      'the generic CORS wording belongs to dragImageUrl.js, which names the host');
  } finally { doc.restore(); }
});

test('the chat-attach path words its failure the same way', async () => {
  const { doc, toasts, dataTransfer } = refusedUrlDrag();
  try {
    const host = createStubElement('div');
    host.classList.add('chat-open');
    const dropRow = createStubElement('div');
    const attached = [];
    wirePanelDrop({ host, dropRow, attachFiles: async (fs) => { attached.push(...fs); } });
    dropRow.dispatch('drop', { preventDefault() {}, stopPropagation() {}, dataTransfer });
    await settle();
    assert.deepEqual(attached, []);
    assert.equal(toasts.length, 1);
    assert.match(toasts[0].msg, /^Couldn't attach that image — the only URL in that drag failed; blocked\.example refused/);
    assert.ok(toasts[0].msg.endsWith(HINT), 'both drop paths offer the same way out');
  } finally { doc.restore(); }
});
