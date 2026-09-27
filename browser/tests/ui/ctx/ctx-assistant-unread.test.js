// The toolbar unread affordance and the server-session cards (js/llm/chat/session.js): only
// work in flight marks the button, and an expired session gets a Reconnect CTA.
import { test } from 'node:test';
import assert from 'node:assert';
import { describeChatError } from '../../../js/llm/chat/session.js';
import { LlmError } from '../../../js/llm/client.js';
import { COMPONENTS_CSS } from '../../helpers/css.js';
import { wireBothSurfaces, typeAndSend } from '../../helpers/chatSurfacesRig.js';

const tick = () => new Promise((r) => setTimeout(r, 0));

// ── Unread affordance on the toolbar button ────────────────────────────────
test('a turn landing on a closed chat toasts, and only WORK IN FLIGHT marks the button', async () => {
  const s = await wireBothSurfaces();
  const btn = s.doc.getElementById('chat-btn');
  // In flight behind a closed chat: the quiet pulse, cleared once the turn lands.
  typeAndSend(s.panel, 'outline it');
  assert.ok(btn.classList.contains('chat-working'), 'work in flight pulses the button');
  s.ctrl.settle({ reply: 'Done.', results: [] });
  await tick();
  assert.strictEqual(btn.classList.contains('chat-working'), false, 'cleared in cleanup');
  // The toast fires only while no surface can show the answer, and itself opens the chat…
  assert.strictEqual(s.notices.length, 1);
  s.notices[0].opts.onClick();
  assert.ok(s.panel.host.classList.contains('chat-open'), 'the toast itself opens the chat');
  // …and it leaves NOTHING behind on the icon: no unread badge on either surface.
  assert.ok(!/unread/.test(btn.className + s.panel.host.className), 'no unread dot is marked anywhere');
  typeAndSend(s.panel, 'again');
  assert.strictEqual(btn.classList.contains('chat-working'), false, 'an open chat shows its own progress');
  s.ctrl.settle({ reply: 'Done.', results: [] });
  await tick();
  assert.strictEqual(s.notices.length, 1, 'never while the chat is visible');
  // A pure pseudo-element dot: the button's box never moves.
  const css = COMPONENTS_CSS;
  assert.ok(!css.includes('chat-unread'), 'and no unread rule is left in the stylesheet');
  const dot = css.slice(css.indexOf('#chat-btn.chat-working::before'), css.indexOf('/* On the accent-filled'));
  assert.match(dot, /position: absolute/);
  assert.match(dot, /background: var\(--accent\)/, 'accent-coloured');
  assert.match(css, /#chat-btn\.active\.chat-working::before/, 'and it inverts on the accent fill');
});

// stencil-server posts to /llm/chat with the same bearer the projects list uses, so a stale
// token 401s there too: the session is over and reconnect is the cure, not "unreachable".
test('an expired stencil-server session is named as such in the chat card', async () => {
  const { describeChatError } = await import('../../../js/llm/chat/session.js');
  const { LlmError } = await import('../../../js/llm/client.js');
  const http = (msg, status) => Object.assign(new LlmError(msg, 'http'), { status, answered: true });
  const settings = { provider: 'stencil-server', serverUrl: 'http://localhost:8090' };
  const out = describeChatError(http('unauthorized', 401), settings);
  assert.strictEqual(out.kind, 'expired', 'its own kind — not "unreachable"');
  assert.match(out.text, /session on localhost:8090 has expired/);
  assert.match(out.text, /reconnect to that server/);
  assert.strictEqual(out.serverUrl, 'http://localhost:8090', 'the card knows WHICH server');
  // 403 counts too…
  assert.strictEqual(describeChatError(http('forbidden', 403), settings).kind, 'expired');
  // …but the same status from a LOCAL provider is an ordinary unreachable card: there
  // is no Stencil session to renew there.
  const ollama = { provider: 'ollama', baseUrl: 'http://localhost:11434' };
  assert.strictEqual(describeChatError(http('nope', 401), ollama).kind, 'unreachable');
  // …and a plain server error stays unreachable.
  assert.strictEqual(describeChatError(http('boom', 500), settings).kind, 'unreachable');
});

test('the expired card carries a Reconnect CTA instead of Configure provider', async () => {
  const s = await wireBothSurfaces();
  localStorage.setItem('drawingApp_llmSettings', JSON.stringify({ provider: 'stencil-server', serverUrl: 'http://localhost:8090' }));
  let connections = 0;
  s.doc.getElementById('connect-btn').click = () => { connections += 1; };
  // The row patch marks the expired turn, and the renderer picks the CTA off that mark…
  typeAndSend(s.panel, 'hi');
  s.ctrl.fail(Object.assign(new LlmError('unauthorized', 'http'), { status: 401, answered: true }));
  await tick();
  const row = s.session.chatLog().at(-1);
  assert.deepStrictEqual([row.card, row.reconnect], [true, 'http://localhost:8090']);
  const ctas = (t) => ['.chat-reconnect-cta', '.chat-config-cta'].map((c) => t.querySelectorAll(c).length);
  // …exactly one of the two lives on a row, and both surfaces route it to Connections.
  for (const [name, surf] of [['panel', s.panel], ['flyout', s.flyout]]) {
    assert.deepStrictEqual(ctas(surf.transcript), [1, 0], `${name} shows Reconnect only`);
    surf.transcript.querySelector('.chat-reconnect-cta').fire('click');
  }
  assert.deepStrictEqual([connections, s.host.closed], [2, 1], 'both open Connections; the flyout closes its menu first');
  s.session.updateChatRow(row.id, { reconnect: null });
  assert.deepStrictEqual(ctas(s.panel.transcript), [0, 1], 'an unreachable card takes Configure instead');
  s.session.updateChatRow(row.id, { card: false });
  assert.deepStrictEqual(ctas(s.panel.transcript), [0, 0], 'and a settled row neither');
});

test('chatReconnectButton names the server it will sign in to', async () => {
  globalThis.document = {
    createElement: () => ({ className: '', innerHTML: '', _l: {},
      addEventListener(t, fn) { this._l[t] = fn; }, click() { this._l.click?.(); } }),
  };
  const { chatReconnectButton } = await import('../../../js/ui/chat/view.js?reconnect-cta');
  let asked = null;
  const b = chatReconnectButton('http://localhost:8090', (u) => { asked = u; });
  assert.ok(b.className.includes('chat-reconnect-cta'));
  assert.match(b.innerHTML, /Reconnect to localhost:8090/, 'the scheme is dropped, the host is not');
  b.click();
  assert.strictEqual(asked, 'http://localhost:8090', 'and it hands the URL back');
  // No URL known → a plain label, never "Reconnect to undefined".
  assert.match(chatReconnectButton('', () => {}).innerHTML, /<span>Reconnect<\/span>/);
});
