// The toolbar unread affordance and the server-session cards (js/llm/session.js): only
// work in flight marks the button, and an expired session gets a Reconnect CTA.
import { test } from 'node:test';
import assert from 'node:assert';
import { readFileSync } from 'node:fs';
import { describeChatError } from '../../../js/llm/chat/session.js';
import { LlmError } from '../../../js/llm/client.js';
import { COMPONENTS_CSS } from '../../helpers/css.js';
import { chatViewSource } from '../../helpers/chatViewSource.js';
import { contextMenuSource } from '../../helpers/contextMenuSource.js';

// ── Unread affordance on the toolbar button ────────────────────────────────
test('a turn landing on a closed chat toasts, and only WORK IN FLIGHT marks the button', () => {
  const panel = readFileSync(new URL('../../../js/ui/chat/panel.js', import.meta.url), 'utf8');
  // The toast fires only while no surface can show the answer…
  const closed = panel.slice(panel.indexOf('const closedToast = (res) => {'), panel.indexOf('// ── Send loop'));
  assert.ok(closed.includes('if (panelIsOpen() || !toast) return;'), 'never while the chat is visible');
  assert.ok(closed.includes('onClick: () => setOpen(true)'), 'and the toast itself opens the chat');
  // …and it leaves NOTHING behind on the icon: no unread badge on either surface
  // (the desktop's twin went with it — MainWindowChat.cpp).
  assert.ok(!panel.includes('chat-unread') && !panel.includes('markChatUnread'),
    'no unread dot is marked anywhere');
  // In-flight behind a closed chat still gets the quiet pulse, cleared in cleanup.
  assert.ok(panel.includes('markChatBusy(!panelIsOpen());'));
  assert.ok(panel.includes('markChatBusy(false);'));
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
  const view = chatViewSource();
  // The row patch marks it, the renderer picks the CTA off that mark…
  const session = readFileSync(new URL('../../../js/llm/chat/session.js', import.meta.url), 'utf8');
  assert.match(session, /card: res\.kind === 'unreachable' \|\| res\.kind === 'expired'/);
  assert.match(session, /reconnect: res\.kind === 'expired' \? \(res\.serverUrl \|\| ''\) : null/);
  assert.ok(view.includes("const cta = row.reconnect ? '.chat-reconnect-cta' : '.chat-config-cta';"));
  assert.ok(view.includes('chatReconnectButton(row.reconnect, onReconnect)'));
  // …exactly one of the two lives on a row, whichever it is.
  assert.ok(view.includes("if (!row.card || row.reconnect) el.querySelector('.chat-config-cta')?.remove();"));
  assert.ok(view.includes("if (!row.card || !row.reconnect) el.querySelector('.chat-reconnect-cta')?.remove();"));
  // Both surfaces route it to the Connections modal.
  for (const [f, src] of [['panel.js', readFileSync(new URL('../../../js/ui/chat/panel.js', import.meta.url), 'utf8')],
    ['contextMenu.js', contextMenuSource()]]) {
    assert.ok(/onReconnect: \(\) =>/.test(src), `${f} wires the hook`);
    assert.ok(src.includes("document.getElementById('connect-btn')?.click()"), `${f} opens Connections`);
  }
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
