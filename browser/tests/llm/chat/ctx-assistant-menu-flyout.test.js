// The context menu's live flyouts (js/ui/contextMenu/ + js/ui/ctx/): both entries settled
// before the menu is measured, the Assistant re-synced on a provider change, what counts as
// an engaged flyout, and the only four paths out of the menu from inside the chat.
import { test } from 'node:test';
import assert from 'node:assert';
import { wireContextMenu } from '../../helpers/ctxMenuChatRig.js';
import { wireBothSurfaces, makeEl } from '../../helpers/chatSurfacesRig.js';
import { publish, EVENTS } from '../../../js/eventBus/appBus.js';

const tick = () => new Promise((r) => setTimeout(r, 0));

test('both flyout entries are settled before the menu is measured, and re-synced on a provider change', async () => {
  const m = await wireContextMenu();
  const assist = m.flyout.item;
  m.scriptItem.dataset.noSub = 'stale';
  assist.dataset.noSub = 'stale';
  let atMeasure = null;
  Object.defineProperty(m.menu, 'offsetWidth', { get() {
    atMeasure ??= [assist.dataset.noSub, m.scriptItem.dataset.noSub];
    return 120;
  } });
  m.openAt();
  assert.deepStrictEqual(atMeasure, ['0', '0'], 'the Assistant and Script rows are re-moded first');
  localStorage.setItem('drawingApp_llmSettings', JSON.stringify({ provider: 'none' }));
  publish(EVENTS.llmSettingsChanged);
  assert.strictEqual(assist.style.display, 'none', 'no provider hides the entry');
  localStorage.setItem('drawingApp_llmSettings', JSON.stringify({ provider: 'ollama' }));
  publish(EVENTS.llmSettingsChanged);
  assert.strictEqual(assist.style.display, '', 'a provider brings it back without a reload');
});

test('typing, resizing the composer or a running turn engage the flyout; hideSub honours it', async () => {
  const s = await wireBothSurfaces();
  const flyout = s.flyout.el;
  assert.strictEqual(flyout._keepOpen(), false);
  s.flyout.input.focus();
  assert.strictEqual(flyout._keepOpen(), true, 'typing in the flyout');
  s.doc.activeElement = null;
  s.doc.getElementById('ctx-assist-sizer').fire('pointerdown', { clientY: 10 });
  assert.strictEqual(flyout._keepOpen(), true, 'mid composer resize');
  window.fire('pointerup');
  assert.strictEqual(flyout._keepOpen(), false, 'the resize ended');
  s.host.sendingFlag = true;
  assert.strictEqual(flyout._keepOpen(), true, 'a running turn');
  s.host.sendingFlag = false;
  // The hover-out path skips an engaged flyout, and closes a disengaged one.
  const { createCtxNav } = await import('../../../js/ui/contextMenu/nav.js');
  const nav = createCtxNav({ menu: makeEl() });
  const item = makeEl();
  const sub = makeEl();
  sub.classList.add('ctx-sub-visible');
  sub._keepOpen = () => true;
  nav.hideSub(item, sub);
  assert.ok(sub.classList.contains('ctx-sub-visible'), 'an engaged flyout stays');
  sub._keepOpen = () => false;
  nav.setLastPointer(500, 500);
  nav.hideSub(item, sub);
  assert.strictEqual(sub.classList.contains('ctx-sub-visible'), false, 'a disengaged one closes');
});

test('chatting never closes the menu — only the gear, the two CTAs and the phone hand-over do', async () => {
  const s = await wireBothSurfaces();
  const $ = (id) => s.doc.getElementById(id);
  s.flyout.input.value = 'crop';
  s.flyout.input.fire('keydown', { key: 'Enter' });
  s.ctrl.fail(new Error('boom'));
  await tick();
  s.flyout.el.fire('click');
  s.flyout.transcript.querySelector('.chat-retry-cta').fire('click');
  s.ctrl.settle({ reply: 'ok', results: [] });
  await tick();
  $('ctx-assist-clear').fire('click');
  assert.strictEqual(s.host.closed, 0, 'sending, a failure, retrying and clearing leave the menu open');
  $('ctx-assist-settings-btn').fire('click');
  assert.strictEqual(s.host.closed, 1, 'the gear opens settings over a closed menu');
  s.session.appendChatRow({ role: 'assistant', text: 'down', error: true, card: true });
  s.flyout.transcript.querySelector('.chat-config-cta').fire('click');
  assert.strictEqual(s.host.closed, 2, 'Configure provider');
  s.session.appendChatRow({ role: 'assistant', text: 'expired', error: true, card: true, reconnect: 'http://srv' });
  let reconnected = 0;
  $('connect-btn').click = () => { reconnected += 1; };
  s.flyout.transcript.querySelector('.chat-reconnect-cta').fire('click');
  assert.deepStrictEqual([s.host.closed, reconnected], [3, 1], 'Reconnect');
  s.flyout.item.dataset.noSub = '1';
  s.flyout.item.fire('click', { target: s.flyout.item });
  assert.deepStrictEqual([s.host.closed, s.handovers], [4, ['panel']], 'the phone hand-over to the panel');
});
