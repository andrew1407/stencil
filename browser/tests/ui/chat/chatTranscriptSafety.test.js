// Invariant: the chat transcript renders MODEL text as data. Every string a row carries is
// rendered through renderChatLog and must land as text, never as markup; the two sink lints
// stay on the source, since a negative over every code path is what no fixture can reach.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { chatViewSource } from '../../helpers/chatViewSource.js';
import { descendants, makeEl, stubDom, rowsOf, textNodesOf } from '../../helpers/chatTranscriptRig.js';

const SRC = chatViewSource();
const INNER_HTML = [...SRC.matchAll(/[\w.$]+\.innerHTML\s*=\s*([^;]+);/g)].map((m) => m[1].trim());
const EVIL = '<img src=x onerror=alert(1)>';

const hostileLog = () => [
  { id: 1, role: 'user', text: `hi ${EVIL}`, attachments: [{ name: EVIL, kind: 'image', dataUrl: 'data:,' }] },
  { id: 2, role: 'assistant', text: EVIL, results: [{ label: EVIL, dataUrl: 'data:,' }],
    ask: { question: EVIL, options: [{ label: EVIL }], allowCustom: true, customLabel: EVIL } },
  { id: 3, role: 'assistant', text: EVIL, error: true, card: true, retryText: EVIL },
  { id: 4, role: 'assistant', text: EVIL, error: true, card: true, reconnect: `https://${EVIL}` },
];

const paint = async (log, tag) => {
  stubDom();
  const { renderChatLog } = await import(`../../../js/ui/chat/view.js?safety-${tag}`);
  const transcript = makeEl();
  renderChatLog(transcript, log, { onRetry() {}, onReconnect() {}, onConfigure() {} });
  return transcript;
};

test('the model reply lands in the row text node via textContent', async () => {
  const transcript = await paint([{ id: 1, role: 'assistant', text: EVIL }], 'text');
  const [text] = textNodesOf(rowsOf(transcript)[0]);
  assert.equal(text.textContent, EVIL, 'the reply is the text node\'s text, verbatim');
  assert.equal(text.innerHTML, '', 'and never its markup');
});

test('no model/log string ever reaches innerHTML', async () => {
  const transcript = await paint(hostileLog(), 'every-row');
  const nodes = descendants(transcript);
  assert.ok(nodes.some((n) => n.innerHTML), 'the icon buttons did render markup');
  for (const n of nodes) assert.ok(!String(n.innerHTML).includes('<img'), `innerHTML = ${n.innerHTML}`);
  const texts = nodes.map((n) => n._text);
  assert.ok(texts.filter((t) => t.includes(EVIL)).length >= 5, 'reply, result, question, option and error as text');
});

test('every innerHTML value is a built-in icon or literal, escaping what it interpolates', async () => {
  const transcript = await paint(hostileLog(), 'escape');
  const cta = descendants(transcript).find((n) => n.classList.contains('chat-reconnect-cta'));
  assert.ok(cta.innerHTML.includes('Reconnect to &lt;img src=x onerror=alert(1)&gt;'),
    'the one interpolated host is escaped');
  const CALLABLE = new Set(['escapeHtml', 'icon']);
  for (const rhs of INNER_HTML) {
    for (const [, token, next] of rhs.matchAll(/\$\{\s*([A-Za-z_$][\w.$]*)\s*([(?]?)/g)) {
      if (next === '?') continue;   // a ternary TEST is never itself rendered
      assert.equal(next, '(', `innerHTML interpolates \${${token}} raw — wrap it in escapeHtml()`);
      assert.ok(CALLABLE.has(token), `innerHTML interpolates ${token}() — escape it`);
    }
  }
});

test('the transcript parses no HTML by any other route', () => {
  for (const sink of ['insertAdjacentHTML', 'outerHTML =', 'createContextualFragment', 'document.write', 'DOMParser']) {
    assert.ok(!SRC.includes(sink), `${sink} in view.js`);
  }
});
