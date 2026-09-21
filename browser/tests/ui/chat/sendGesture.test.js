import { test } from 'node:test';
import assert from 'node:assert';
import { createSendGesture, syncComposerControls, SEND_TITLE, SEND_TITLE_PLAIN, VOICE_TITLE_LISTENING, VOICE_TITLE_PAUSED } from '../../../js/ui/chat/view.js';
import { DOUBLE_CLICK_MS, LONG_PRESS_MS } from '../../../js/ui/tip/popover.js';
import { stubClock } from '../../helpers/speech.js';
import { createStubElement } from '../../helpers/dom.js';
import { chatViewSource } from '../../helpers/chatViewSource.js';

// The send button's gesture (view.js createSendGesture): a plain click acts one
// double-click interval later, a double-click or a hold switches typing ↔ dictation.
const make = () => {
  const clock = stubClock();
  const log = [];
  const g = createSendGesture({
    onClick: () => log.push('click'), onSwitch: () => log.push('switch'),
    setTimer: clock.setTimer, clearTimer: clock.clearTimer,
  });
  return { clock, log, g };
};

test('a plain click acts after the double-click interval, never before', () => {
  const { clock, log, g } = make();
  g.pressStart({ x: 0, y: 0 }); g.pressEnd(); g.click();
  clock.advance(DOUBLE_CLICK_MS - 1);
  assert.deepStrictEqual(log, []);
  clock.advance(1);
  assert.deepStrictEqual(log, ['click']);
});

test('a double-click switches once and swallows both clicks', () => {
  const { clock, log, g } = make();
  g.pressStart(); g.pressEnd(); g.click();
  g.pressStart(); g.pressEnd(); g.click();
  g.dblclick();
  clock.advance(DOUBLE_CLICK_MS * 2);
  assert.deepStrictEqual(log, ['switch']);
});

test('a hold switches at the long-press mark and the click on release is not a send', () => {
  const { clock, log, g } = make();
  g.pressStart({ x: 5, y: 5 });
  clock.advance(LONG_PRESS_MS);
  assert.deepStrictEqual(log, ['switch']);
  g.pressEnd(); g.click();
  clock.advance(DOUBLE_CLICK_MS);
  assert.deepStrictEqual(log, ['switch'], 'the release click is swallowed');
  // A short press is an ordinary click.
  g.pressStart(); clock.advance(LONG_PRESS_MS - 1); g.pressEnd(); g.click();
  clock.advance(DOUBLE_CLICK_MS);
  assert.deepStrictEqual(log, ['switch', 'click']);
});

test('moving past the slop cancels the hold', () => {
  const { clock, log, g } = make();
  g.pressStart({ x: 0, y: 0 });
  g.pressMove({ x: 30, y: 0 });
  clock.advance(LONG_PRESS_MS);
  assert.deepStrictEqual(log, []);
});

// ── The three faces (syncComposerControls) ──
const face = (text, sending, opts) => {
  const sendBtn = createStubElement('button');
  const attachBtn = createStubElement('button');
  const input = createStubElement('textarea', { value: text });
  syncComposerControls({ sendBtn, attachBtn, input }, sending, opts);
  return { sendBtn, attachBtn };
};

test('send face: plain Send when voice is unavailable (disabled while empty), gesture hint when it is', () => {
  let { sendBtn } = face('', false, {});
  assert.strictEqual(sendBtn.disabled, true);
  assert.strictEqual(sendBtn.dataset.title, SEND_TITLE_PLAIN);
  assert.ok(!sendBtn.classList.contains('chat-send-idle'));
  ({ sendBtn } = face('', false, { voiceSupported: true }));
  assert.strictEqual(sendBtn.disabled, false, 'stays clickable so a hold / double-click can switch to dictation');
  assert.strictEqual(sendBtn.getAttribute('aria-disabled'), 'true');
  assert.ok(sendBtn.classList.contains('chat-send-idle'));
  assert.strictEqual(sendBtn.dataset.title, SEND_TITLE);
  ({ sendBtn } = face('hello', false, { voiceSupported: true }));
  assert.strictEqual(sendBtn.getAttribute('aria-disabled'), 'false');
  assert.ok(!sendBtn.classList.contains('chat-send-idle'));
  assert.ok(sendBtn.innerHTML.includes('ic-send'));
  assert.ok(!sendBtn.dataset.title.includes('Enter'), 'no Enter keycap on the send tooltip any more');
});

test('mic face: paused vs listening titles and classes; Stop wins while a turn runs', () => {
  let { sendBtn } = face('', false, { voiceSupported: true, voice: { on: true, listening: false } });
  assert.strictEqual(sendBtn.disabled, false);
  assert.ok(sendBtn.innerHTML.includes('ic-mic'));
  assert.strictEqual(sendBtn.dataset.title, VOICE_TITLE_PAUSED);
  assert.ok(sendBtn.classList.contains('chat-voice-on') && !sendBtn.classList.contains('chat-voice-listening'));
  ({ sendBtn } = face('partial', false, { voiceSupported: true, voice: { on: true, listening: true } }));
  assert.strictEqual(sendBtn.dataset.title, VOICE_TITLE_LISTENING);
  assert.ok(sendBtn.classList.contains('chat-voice-listening'));
  let r = face('partial', true, { voiceSupported: true, voice: { on: true, listening: true } });
  assert.ok(r.sendBtn.innerHTML.includes('ic-stop'));
  assert.strictEqual(r.sendBtn.dataset.title, 'Stop the response');
  assert.ok(!r.sendBtn.classList.contains('chat-voice-on'));
  assert.strictEqual(r.attachBtn.disabled, true);
});

// A double-click, a hold or the "…" item only SWITCHES the composer to voice input: the mic face shows
// paused, and a click on it starts listening (user report).
test('switching to voice input lands on a paused mic face — nothing starts listening', async () => {
  const { readFileSync } = await import('node:fs');
  const src = chatViewSource();
  const body = src.slice(src.indexOf('const toggleMode = () => {'), src.indexOf('const toggleListening'));
  assert.ok(!body.includes('startListening()'), 'the switch never starts the engine');
  assert.ok(body.includes('setFace(true);'), 'it only turns the face');
  assert.ok(body.includes('if (on) { stopListening(); setFace(false);'), 'switching back still stops dictation');
  const { SEND_TITLE } = await import('../../../js/ui/chat/view.js');
  assert.match(SEND_TITLE, /Double-click or hold for voice input/);
});

// The composer's mic face survives the END of an utterance however it ended — another mode taking the mic,
// its own pause, a silence timeout, a spoken "send"; only a FATAL error returns the send plane.
test('the composer keeps its mic face whenever listening ends', async () => {
  const { readFileSync } = await import('node:fs');
  const src = chatViewSource();
  assert.ok(src.includes("onStop: (reason) => { if (reason === 'error') setFace(false); else sync(); }"));
});

// …and the toolbar's hands-free voice chat leaves this composer's FACE alone: the mic face here is the
// user's own choice (the "…" item, a double-click, a hold).
test('the toolbar voice chat never turns the composer to the mic face by itself', async () => {
  const { readFileSync } = await import('node:fs');
  const src = chatViewSource();
  assert.ok(src.includes('subscribe(VOICE_STATE_EVENT, () => sync(), { target: win });'),
    'the voice-state listener only re-syncs the controls');
  const listener = src.slice(src.indexOf('subscribe(VOICE_STATE_EVENT'),
    src.indexOf('return {', src.indexOf('subscribe(VOICE_STATE_EVENT')));
  assert.ok(!listener.includes('setFace('), 'and never sets the face behind the user');
});
