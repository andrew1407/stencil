// ui/llmSettings/keyRows.js — the anthropic session-key rows: Save holds the typed key for this tab
// (never localStorage), the field is never filled back, Forget drops it, the status line names the
// local expiry, and a browser refusing session storage keeps the dialog on the reason.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createStubElement } from '../../helpers/dom.js';
import { createMemoryStorage, installMemoryStorage } from '../../helpers/memoryStorage.js';
import { layout } from '../../../js/ui/layout.js';

const local = installMemoryStorage();
const tab = createMemoryStorage();
Object.defineProperty(globalThis, 'sessionStorage', { value: tab, configurable: true, writable: true });

const { wireSessionKeyRows, sessionKeyStatusText, SESSION_STORAGE_BLOCKED_TEXT } = await import('../../../js/ui/llmSettings/keyRows.js');
const { readSessionKey, setSessionKey, forgetSessionKey } = await import('../../../js/llm/sessionKey.js');

const KEY = 'sk-ant-test-0123456789abcdef';
const IDS = ['chat-session-key-row', 'chat-session-key', 'chat-session-key-status-row',
  'chat-session-key-status', 'chat-session-key-forget', 'chat-session-key-note'];

const dialog = () => {
  const els = Object.fromEntries(IDS.map((id) => [id, createStubElement(id === 'chat-session-key' ? 'input' : 'div')]));
  const host = { querySelector: (sel) => els[sel.slice(1)] || null };
  return { els, host };
};

test('the rows are in the dialog markup, the field a password field that no autofill keeps', () => {
  const markup = layout();
  for (const id of IDS) assert.equal(markup.split(`id="${id}"`).length, 2, id);
  assert.match(markup, /<input type="password" id="chat-session-key" autocomplete="off"/);
  assert.match(markup, /<option value="anthropic">Anthropic API \(Claude\)<\/option>/);
  assert.match(markup, /straight from this page to Anthropic[\s\S]*12 hours[\s\S]*never saved/);
});

test('Save holds the typed key for this tab only and empties the field', () => {
  local.clear(); tab.clear();
  const { els, host } = dialog();
  const rows = wireSessionKeyRows(host);
  rows.show(true);
  assert.equal(els['chat-session-key-row'].style.display, '');
  assert.equal(els['chat-session-key-status'].textContent, 'No key for this session.');
  assert.equal(els['chat-session-key-forget'].disabled, true);
  els['chat-session-key'].value = `  ${KEY} `;
  assert.equal(rows.requestKey(), KEY, 'a probe may use the key being typed');
  assert.equal(rows.commit(), true);
  assert.equal(readSessionKey()?.key, KEY);
  assert.equal(els['chat-session-key'].value, '');
  assert.match(els['chat-session-key-status'].textContent, /^Key kept for this tab until .+\.$/);
  assert.equal(els['chat-session-key-forget'].disabled, false);
  assert.equal(local._map.size, 0, 'nothing reached localStorage');
});

test('showing never fills the field back; hiding hides every row; Forget drops the key', () => {
  tab.clear();
  setSessionKey(KEY);
  const { els, host } = dialog();
  let forgot = 0;
  const rows = wireSessionKeyRows(host, { onForget: () => { forgot++; } });
  rows.show(true);
  assert.equal(els['chat-session-key'].value, '');
  assert.equal(rows.requestKey(), KEY, 'an empty field falls back to the held key');
  rows.show(false);
  for (const id of ['chat-session-key-row', 'chat-session-key-status-row', 'chat-session-key-note']) {
    assert.equal(els[id].style.display, 'none', id);
  }
  els['chat-session-key-forget'].dispatch('click');
  assert.equal(readSessionKey(), null);
  assert.equal(forgot, 1);
  assert.equal(els['chat-session-key-status'].textContent, 'No key for this session.');
  assert.equal(rows.commit(), true, 'an empty field on Save leaves nothing to hold');
  assert.equal(readSessionKey(), null);
});

test('the status names the local expiry, with the weekday once it is another day', () => {
  const now = new Date(2026, 8, 27, 9, 0).getTime();
  const at = new Date(2026, 8, 27, 21, 0);
  const clock = at.toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' });
  assert.equal(sessionKeyStatusText({ key: 'k', expiresAt: at.getTime() }, now), `Key kept for this tab until ${clock}.`);
  const next = new Date(2026, 8, 28, 3, 30);
  const weekday = next.toLocaleDateString([], { weekday: 'short' });
  assert.equal(sessionKeyStatusText({ key: 'k', expiresAt: next.getTime() }, now),
    `Key kept for this tab until ${weekday} ${next.toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' })}.`);
  assert.equal(sessionKeyStatusText(null), 'No key for this session.');
});

test('a browser refusing session storage keeps the dialog on the reason; missing rows are inert', () => {
  const refusing = { getItem: () => null, setItem() { throw new Error('SecurityError'); }, removeItem() {} };
  Object.defineProperty(globalThis, 'sessionStorage', { value: refusing, configurable: true, writable: true });
  const { els, host } = dialog();
  const rows = wireSessionKeyRows(host);
  els['chat-session-key'].value = KEY;
  assert.equal(rows.commit(), false);
  assert.equal(els['chat-session-key-status'].textContent, SESSION_STORAGE_BLOCKED_TEXT);
  Object.defineProperty(globalThis, 'sessionStorage', { value: tab, configurable: true, writable: true });
  const bare = wireSessionKeyRows({ querySelector: () => null });
  assert.doesNotThrow(() => { bare.show(true); bare.commit(); });
  assert.equal(wireSessionKeyRows(null).requestKey(), '');
  forgetSessionKey();
});
