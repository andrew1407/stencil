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

const fileDrag = (type) => ({ types: ['Files'], files: [], items: [{ kind: 'file', type }], getData: () => '' });

test('a dragged .json shows one layout zone; an image or a .stencil shows the split', () => {
  const doc = installDom({}, { window: { innerWidth: 1000, innerHeight: 800 } });
  try {
    const overlay = createStubElement('div');
    doc.register('global-drop-overlay', overlay);
    wireDropPaste({ image: null });
    const enter = (type) => doc.dispatch('dragenter', { preventDefault() {}, clientX: 10, clientY: 10, dataTransfer: fileDrag(type) });
    enter('application/json');
    assert.ok(overlay.classList.contains('drop-single'));
    assert.equal(overlay.dataset.dropKind, 'layout:false', 'worded for a window with no image');
    enter('image/png');
    assert.ok(!overlay.classList.contains('drop-single'));
    enter('');
    assert.ok(!overlay.classList.contains('drop-single'), 'a .stencil opens saved or incognito');
  } finally { doc.restore(); }
});

test('a .stencil dropped on the right half opens incognito', () => {
  const doc = installDom({}, { window: { innerWidth: 1000, innerHeight: 800 } });
  try {
    doc.register('global-drop-overlay', createStubElement('div'));
    const opened = [];
    wireDropPaste({ image: null, export: { openProjectFile: (f, o) => opened.push([f.name, o.incognito]) } });
    const drop = (x) => doc.dispatch('drop', { preventDefault() {}, clientX: x, clientY: 10,
      dataTransfer: { types: ['Files'], files: [{ name: 'trip.stencil', type: '' }], getData: () => '' } });
    drop(800);
    drop(100);
    assert.deepEqual(opened, [['trip.stencil', true], ['trip.stencil', false]]);
  } finally { doc.restore(); }
});
