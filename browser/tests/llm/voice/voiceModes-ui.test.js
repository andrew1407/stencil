// The listening mic's own decoration and the hands-free surface: the ray ring, the toast that
// never marks the chat unread, the unlatched logo and a dictation pause. From voiceModes.test.js.
import { test } from 'node:test';
import assert from 'node:assert';
import { ANIMATIONS_CSS } from '../../helpers/css.js';
import { make, fakeEngine } from '../../helpers/voiceModesRig.js';
import { createVoiceModes } from '../../../js/llm/voice/modes.js';
import { installVoiceRig } from '../../helpers/voiceInstallRig.js';
import { installDom, createStubElement } from '../../helpers/dom.js';
import { closedTurnToast } from '../../../js/llm/chat/session.js';

// The toolbar wired on a stub host with a live voice coordinator; every element it reaches is kept.
const toolbarWithVoice = (StencilToolbar) => {
  const reached = [];
  const keep = (el) => { reached.push(el); return el; };
  const byId = new Map();
  const mic = createStubElement('button', { id: 'voice-chat-btn' });
  const win = createStubElement('window', { dispatchEvent: (ev) => { win.dispatch(ev.type, ev); return true; } });
  const doc = installDom({
    getElementById: (id) => (byId.has(id) ? byId.get(id) : byId.set(id, keep(createStubElement('div', { id }))).get(id)),
    createElement: (tag) => keep(createStubElement(tag)),
    querySelectorAll: (sel) => (sel === '#voice-chat-btn' ? [mic] : []),
  }, {
    window: win,
    MutationObserver: class { observe() {} disconnect() {} },
    requestAnimationFrame: () => 1, cancelAnimationFrame() {},
  });
  const logoWrap = keep(createStubElement('span'));
  const logo = keep(createStubElement('img', { closest: () => logoWrap, parentElement: logoWrap }));
  const host = createStubElement('stencil-toolbar', {
    querySelector: (sel) => ({ '.app-logo': logo, '#voice-chat-btn': mic })[sel] || keep(createStubElement('div')),
    querySelectorAll: () => [],
  });
  const voice = createVoiceModes({ engine: fakeEngine(), win, loadSettings: () => ({ silenceMs: 1000, language: 'default' }) });
  StencilToolbar.prototype.wire.call(host, { voice });
  const fixed = [doc.body, doc.documentElement, host, logo, logoWrap];
  const classesBesidesMic = () => [...fixed.map((el) => el.className),
    ...reached.map((el) => el.className).filter((c) => /voice/.test(c))];
  return { mic, voice, before: classesBesidesMic(), classesBesidesMic, restore: () => { voice.dispose(); doc.restore(); } };
};

test('a listening mic uncovers its ray ring: no overflow clip, isolated stacking, no glass sweep, level-sized', async () => {
  const css = ANIMATIONS_CSS;
  const rule = (selector) => {
    const at = css.indexOf(selector);
    assert.ok(at >= 0, selector);
    return css.slice(at, css.indexOf('}', at));
  };
  const lifted = rule('#voice-chat-btn.active, .chat-abtn.chat-voice-listening, .ctx-assist-abtn.chat-voice-listening {');
  assert.match(lifted, /overflow:\s*visible/);
  assert.match(lifted, /isolation:\s*isolate/);
  // The glass sweep's pseudo becomes the tile's halo: the logo's breathing glow, alive at
  // silence and widened by the voice, spelled to outrank layout.css's hover sweep.
  const halo = rule('#voice-chat-btn.active::after,\n.btn-icon.chat-abtn.chat-voice-on.chat-voice-listening::after');
  assert.match(halo, /content:\s*""/);
  assert.match(halo, /box-shadow:\s*0 0 calc\(7px \+ 14px \* var\(--voice-level, 0\)\)/, 'the halo grows with the voice');
  assert.match(halo, /animation:\s*micHaloPulse/);
  assert.match(css, /@keyframes micHaloPulse \{ 0%, 100% \{ opacity: 0\.55; \} 50% \{ opacity: 1; \} \}/);
  // …and the mic tiles wear no browser focus ring over it.
  assert.match(rule('#voice-chat-btn:focus, #voice-chat-btn:focus-visible,'), /outline:\s*none/);
  // The mics' ring is no longer CSS — it rides the dust canvas (voiceDust.test.js), where
  // the tile is punched out, so no ::before ring rule may target the mics.
  assert.ok(!/#voice-chat-btn(\.active)?::before/.test(css), 'no CSS ring on the toolbar mic');
  assert.ok(!/chat-voice-(on|listening)::before/.test(css), 'no CSS ring on the composer mics');
  assert.ok(!css.includes('micRaysShimmer'));
});

// A spoken answer that lands behind a closed panel is announced by its toast alone: hands-free chat
// never leaves the unread dot on the chat icon (user report).
test('voice-chat answers behind a closed panel toast but never mark the chat icon unread', async () => {
  const rig = installVoiceRig({ entry: { reply: 'Made it sepia.', results: [] } });
  try {
    await rig.say('make it sepia');
    const answer = rig.toasts.at(-1);
    assert.strictEqual(rig.toasts.length, 2, 'the Sent balloon, then the answer');
    assert.deepStrictEqual([answer.msg, answer.type], [closedTurnToast({ ok: true, entry: { reply: 'Made it sepia.', results: [] } }).text, 'ok']);
    assert.deepStrictEqual(rig.chatCalls, [], 'the voice path marks nothing on the icon');
    answer.opts.onClick();
    assert.deepStrictEqual(rig.chatCalls, ['open'], 'the toast is the way back to the chat');
  } finally { rig.restore(); }
});

// …but an answer that ASKS something back (a §11 ask card) opens the panel instead of toasting: a
// card cannot be answered from a balloon.
test('a voice answer that asks a question opens the chat; a plain one still just toasts', async () => {
  const ask = { question: 'Which filter?', mode: 'single', options: [{ label: 'Sepia' }, { label: 'B&W' }] };
  const asking = installVoiceRig({ entry: { reply: 'Which one?', results: [], ask } });
  try {
    await asking.say('make it vintage');
    assert.deepStrictEqual(asking.chatCalls, ['open'], 'the ask card is what opens the panel');
    assert.deepStrictEqual(asking.toasts.map((t) => t.msg.slice(0, 5)), ['Sent:'], 'and the toast path then does not fire');
  } finally { asking.restore(); }
  const plain = installVoiceRig();
  try {
    await plain.say('make it sepia');
    assert.deepStrictEqual(plain.chatCalls, []);
    assert.strictEqual(plain.toasts.length, 2, 'a plain answer still toasts');
  } finally { plain.restore(); }
});

// The logo's shine is its own hover's (and its accent popover's) — switching the mic on
// must not light it (user report). Only the two mic faces wear the voice shine.
test('activating the microphone never latches the logo shine', async () => {
  const { StencilToolbar } = await import('../../../js/ui/toolbar/toolbar.js');
  assert.ok(!StencilToolbar.inner().includes('voice-live'), 'no voice latch in the toolbar markup');
  const bar = toolbarWithVoice(StencilToolbar);
  try {
    bar.mic.dispatch('click');
    assert.strictEqual(bar.voice.voiceChat, true);
    assert.ok(bar.mic.classList.contains('active'), 'the mic wears the voice');
    assert.deepStrictEqual(bar.classesBesidesMic(), bar.before, 'no other element took a class — the logo least of all');
  } finally { bar.restore(); }
  assert.ok(!ANIMATIONS_CSS.includes('voice-live'), 'no voice rule targets the logo');
});

// The composer sends ONLY on the spoken phrase; a pause ends the dictation ('silence') with the words
// left in the textarea, and the button keeps its paused mic face. Voice chat sends on a pause.
test('composer: a pause stops dictation and keeps the words; only "send" submits', () => {
  const m = make({ settings: { silenceMs: 1000 } });
  const { voice, engine, clock, target } = m;
  const t = target();
  assert.strictEqual(voice.startComposer(t), true);
  engine.hear('draw a line');
  assert.deepStrictEqual(t.texts, ['draw a line'], 'dictation lands in the box as it comes');
  clock.advance(1000);
  assert.deepStrictEqual(t.submits, [], 'a pause never sends from the composer');
  assert.deepStrictEqual(t.stops, ['silence'], 'the mic goes off, saying why');
  assert.strictEqual(voice.mode, 'off');
  assert.strictEqual(clock.pending, 0);
  // Back on: the phrase sends — and ends the dictation with it.
  const t2 = target();
  voice.startComposer(t2);
  engine.hear('make it sepia send it');
  assert.deepStrictEqual(t2.texts.at(-1), 'make it sepia');
  assert.deepStrictEqual(t2.submits, ['phrase']);
  assert.strictEqual(voice.mode, 'off', 'and it stops listening after a send');
});
