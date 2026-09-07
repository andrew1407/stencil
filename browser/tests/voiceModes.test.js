import { test } from 'node:test';
import assert from 'node:assert';
import {
  createVoiceModes, splitSendPhrase, SEND_PHRASES, VOICE_STATE_EVENT, VOICE_ACTIVE_LEVEL, UNSUPPORTED_TEXT,
} from '../js/llm/voiceModes.js';
import { VOICE_SETTINGS_EVENT } from '../js/llm/voiceSettings.js';
import { stubClock } from './helpers/speech.js';

// An engine double with the real engine's surface: the test drives transcripts,
// levels and fatal errors through the session it was last started with.
const fakeEngine = ({ supported = true } = {}) => {
  const e = {
    supported, calls: [], session: null, state: 'idle', committed: 0, text: '',
    get listening() { return e.state === 'listening'; },
    get transcript() { return { final: '', interim: e.text, text: e.text }; },
    start(s) { e.calls.push(['start', s.lang]); e.session = s; e.state = 'listening'; s.onState?.('listening'); return true; },
    stop() { e.calls.push(['stop']); e.state = 'idle'; e.session?.onState?.('idle'); e.session = null; },
    commit() { e.committed++; e.text = ''; },
    hear(text) { e.text = text; e.session?.onTranscript?.({ final: '', interim: text, text }); },
    level(l) { e.session?.onLevel?.(l); },
    fail(code) { e.state = 'idle'; e.session?.onError?.({ code, fatal: true, text: `boom ${code}` }); },
  };
  return e;
};
const fakeWindow = () => {
  const listeners = new Map();
  return {
    events: [],
    addEventListener(type, fn) { listeners.set(type, fn); },
    removeEventListener(type) { listeners.delete(type); },
    dispatchEvent(ev) { this.events.push(ev.detail ?? ev.type); listeners.get(ev.type)?.(ev); return true; },
    fire(type) { listeners.get(type)?.({ type }); },
  };
};
const make = ({ supported = true, settings = {} } = {}) => {
  const engine = fakeEngine({ supported });
  const clock = stubClock();
  const win = fakeWindow();
  const sent = [];
  const notes = [];
  let pending = [];
  const cfg = { silenceMs: 1000, language: 'default', ...settings };
  const voice = createVoiceModes({
    engine,
    sendTurn: (text, meta) => new Promise((resolve, reject) => { sent.push(text); pending.push({ text, meta, resolve, reject }); }),
    loadSettings: () => ({ ...cfg }),
    setTimer: clock.setTimer, clearTimer: clock.clearTimer, now: clock.now,
    notify: (text, type) => notes.push([text, type]),
    win,
  });
  const target = () => {
    const t = { texts: [], submits: [], stops: [], setText(s) { t.texts.push(s); }, submit(r) { t.submits.push(r); }, onStop(r) { t.stops.push(r); } };
    return t;
  };
  return { engine, clock, win, sent, notes, cfg, voice, target, get pending() { return pending; }, settle: async () => { for (const p of pending.splice(0)) p.resolve({ ok: true }); await Promise.resolve(); } };
};

test('splitSendPhrase: the four phrases only as the whole tail, case and punctuation tolerant', () => {
  assert.deepStrictEqual(SEND_PHRASES, ['send it', 'execute it', 'send', 'execute']);
  for (const [input, want] of [
    ['crop the image send', { text: 'crop the image', send: true, phrase: 'send' }],
    ['crop the image, send it.', { text: 'crop the image', send: true, phrase: 'send it' }],
    ['Rotate left. Execute!', { text: 'Rotate left', send: true, phrase: 'execute' }],
    ['rotate left EXECUTE IT', { text: 'rotate left', send: true, phrase: 'execute it' }],
    ['send', { text: '', send: true, phrase: 'send' }],
    ['Send it.', { text: '', send: true, phrase: 'send it' }],
    ['resend', { text: 'resend', send: false, phrase: null }],
    ['send me the file', { text: 'send me the file', send: false, phrase: null }],
    ['  many   spaces  ', { text: 'many spaces', send: false, phrase: null }],
    ['', { text: '', send: false, phrase: null }],
    [null, { text: '', send: false, phrase: null }],
  ]) assert.deepStrictEqual(splitSendPhrase(input), want, String(input));
});

test('unsupported: voiceChat=true throws, startComposer is false, nothing starts or fires', () => {
  const { voice, engine, win, target } = make({ supported: false });
  assert.strictEqual(voice.supported, false);
  assert.throws(() => { voice.voiceChat = true; }, new RegExp(UNSUPPORTED_TEXT));
  assert.strictEqual(voice.startComposer(target()), false);
  assert.strictEqual(voice.mode, 'off');
  assert.deepStrictEqual(engine.calls, []);
  assert.deepStrictEqual(win.events, []);
});

test('voice chat on/off: one start in the settings language, a stop, state events each way', () => {
  const { voice, engine, win } = make({ settings: { language: 'uk-UA' } });
  voice.voiceChat = true;
  assert.strictEqual(voice.voiceChat, true);
  assert.strictEqual(voice.mode, 'chat');
  assert.strictEqual(voice.listening, true);
  assert.deepStrictEqual(engine.calls, [['start', 'uk-UA']]);
  voice.voiceChat = true;   // idempotent
  assert.strictEqual(engine.calls.length, 1);
  voice.voiceChat = false;
  assert.strictEqual(voice.mode, 'off');
  assert.deepStrictEqual(engine.calls.at(-1), ['stop']);
  assert.deepStrictEqual(win.events.filter((e) => typeof e === 'object').map((e) => [e.mode, e.listening]),
    [['chat', true], ['chat', true], ['off', false], ['off', false]]);
  voice.voiceChat = false;   // already off — silent
  assert.strictEqual(engine.calls.length, 2);
});

test('exclusivity: composer → chat hot-swaps (no stop) and tells the composer; chat → composer likewise', () => {
  const { voice, engine, target } = make();
  const t = target();
  assert.strictEqual(voice.startComposer(t), true);
  assert.strictEqual(voice.mode, 'composer');
  assert.strictEqual(voice.target, t);
  engine.hear('half a sentence');
  voice.voiceChat = true;
  assert.strictEqual(voice.mode, 'chat');
  assert.deepStrictEqual(t.stops, ['switch']);
  assert.deepStrictEqual(engine.calls.map((c) => c[0]), ['start', 'start'], 'a hot swap, never a stop');
  assert.ok(engine.committed >= 1, 'the half sentence is discarded');
  assert.deepStrictEqual(t.submits, []);
  const t2 = target();
  voice.startComposer(t2);
  assert.strictEqual(voice.voiceChat, false);
  assert.strictEqual(voice.target, t2);
  // Another composer taking over stops the first one's face.
  const t3 = target();
  voice.startComposer(t3);
  assert.deepStrictEqual(t2.stops, ['switch']);
  // stopComposer for a stale target is ignored; for the live one it leaves.
  voice.stopComposer(t2);
  assert.strictEqual(voice.mode, 'composer');
  voice.stopComposer(t3);
  assert.strictEqual(voice.mode, 'off');
  assert.deepStrictEqual(t3.stops, ['stop']);
  // toggleComposer flips.
  assert.strictEqual(voice.toggleComposer(t3), true);
  assert.strictEqual(voice.toggleComposer(t3), false);
  assert.strictEqual(voice.mode, 'off');
});

// Hands-free, the only surface is the toast — and a dictated prompt is a paragraph, not
// a line. The "Sent" balloon echoes just enough of it to prove the mic heard right.
test('the sent balloon echoes a stub of the spoken prompt, never the whole of it', async () => {
  const { spokenEcho, SPOKEN_ECHO_CHARS, CHAT_TOAST_CHARS } = await import('../js/llm/chatSession.js');
  assert.ok(SPOKEN_ECHO_CHARS < CHAT_TOAST_CHARS, 'shorter than a reply balloon');
  assert.strictEqual(spokenEcho('crop it'), 'crop it', 'a short prompt is shown whole');
  const long = spokenEcho('crop ten percent off every edge and then rotate it right twice please');
  assert.strictEqual(long.length, SPOKEN_ECHO_CHARS);
  assert.ok(long.endsWith('…'));
  // One line: a composer's typed prefix can carry newlines into the dictated text.
  assert.strictEqual(spokenEcho('  make it \n  sepia  '), 'make it sepia');
  const { readFileSync } = await import('node:fs');
  const src = readFileSync(new URL('../js/llm/voiceModes.js', import.meta.url), 'utf8');
  assert.ok(src.includes('`Sent: "${spokenEcho(text)}"`'),
    'the voice-chat toast quotes the stub — it is repeating what the mic heard');
});

test('composer: transcripts drive setText, a spoken phrase sends the stripped text and stops', () => {
  const { voice, engine, target } = make();
  const t = target();
  voice.startComposer(t);
  engine.hear('crop the');
  engine.hear('crop the image');
  assert.deepStrictEqual(t.texts, ['crop the', 'crop the image']);
  engine.hear('crop the image send it');
  assert.deepStrictEqual(t.texts.at(-1), 'crop the image');
  assert.deepStrictEqual(t.submits, ['phrase']);
  // One utterance, one message: the spoken send ENDS the dictation (the face stays a
  // paused mic — chatView.js). Hands-free chaining is voice CHAT mode's job.
  assert.strictEqual(voice.mode, 'off', 'the spoken send stops listening');
  assert.deepStrictEqual(t.stops, ['phrase'], 'and says why it stopped');
  // Once for the utterance itself, once as the teardown lets the engine go.
  assert.strictEqual(engine.committed, 2);
  // A bare "send" over an EMPTY box sends nothing — and leaves the mic alone, so a stray
  // "send" heard across a quiet room cannot close a dictation in progress.
  const t2 = target();
  voice.startComposer(t2);
  engine.hear('send');
  assert.deepStrictEqual(t2.submits, []);
  assert.strictEqual(t2.texts.at(-1), '');
  assert.strictEqual(voice.mode, 'composer', 'nothing said, nothing ended');
});

// …but a box that ALREADY holds something is exactly what "send it" is about: words typed
// before the mic went on, or an earlier utterance a pause left standing in the textarea.
// That used to read as "nothing to say" and the message just sat there (user report).
test('composer: a bare "send it" sends what is already in the box, without rewriting it', () => {
  const { voice, engine, target } = make();
  const t = target();
  t.hasText = () => true;            // the composer's own textarea is not empty
  voice.startComposer(t);
  engine.hear('send it');
  assert.deepStrictEqual(t.submits, ['phrase'], 'the standing text is sent');
  // The live transcript still streams in (an utterance can shrink to nothing when the
  // recognizer revises itself), but a bare send never writes WORDS over what stands there:
  // the composer restores whatever it was holding (chatView.js keeps that prefix).
  assert.ok(t.texts.every((x) => x === ''), 'the box is never given the send phrase');
  assert.strictEqual(voice.mode, 'off', 'one utterance, one message — the dictation ends');
  assert.deepStrictEqual(t.stops, ['phrase']);
});

test('silence: arms on results, is held open by recent loud audio only up to the cap, then sends', async () => {
  const m = make({ settings: { silenceMs: 1000 } });
  const { voice, engine, clock, sent } = m;
  voice.voiceChat = true;
  engine.hear('draw a line');
  assert.deepStrictEqual(clock.pendingDelays, [1000]);
  clock.advance(999);
  assert.deepStrictEqual(sent, []);
  clock.advance(1);
  assert.deepStrictEqual(sent, ['draw a line']);
  assert.strictEqual(voice.voiceChat, true, 'voice chat stays on');
  await m.settle();
  // Loud audio (above the threshold) mid-word extends the wait…
  engine.hear('and then');
  clock.advance(800);
  engine.level(VOICE_ACTIVE_LEVEL + 0.1);
  clock.advance(200);   // the original deadline: still loud 200 ms ago → extended
  assert.deepStrictEqual(sent, ['draw a line']);
  clock.advance(800);   // 1000 ms after the last loud frame
  assert.deepStrictEqual(sent, ['draw a line', 'and then']);
  await m.settle();
  // …but continuous noise cannot hold it past twice the setting.
  engine.hear('fan noise');
  for (let i = 0; i < 30; i++) { clock.advance(100); engine.level(0.9); }
  assert.deepStrictEqual(sent.at(-1), 'fan noise');
  assert.strictEqual(clock.pending, 0);
  await m.settle();
  // Quiet audio never counts as speech.
  engine.hear('quiet');
  clock.advance(900);
  engine.level(VOICE_ACTIVE_LEVEL - 0.05);
  clock.advance(100);
  assert.deepStrictEqual(sent.at(-1), 'quiet');
  await m.settle();
  // Stopping clears an armed timer — and sends the words the timer was waiting on.
  engine.hear('unfinished');
  assert.strictEqual(clock.pending, 1);
  voice.stopAll();
  assert.strictEqual(clock.pending, 0);
  assert.deepStrictEqual(sent.at(-1), 'unfinished');
  clock.advance(5000);
  assert.strictEqual(sent.length, 5, 'nothing fires after the stop');
});

test('turning voice chat off sends what was already said; a fatal stop and a composer stop do not', () => {
  const m = make();
  const { voice, engine, sent, target } = m;
  voice.voiceChat = true;
  engine.hear('rotate it a little');
  voice.voiceChat = false;
  assert.deepStrictEqual(sent, ['rotate it a little'], 'the pending words go out on the way off');
  assert.strictEqual(voice.mode, 'off');
  voice.voiceChat = true;
  engine.hear('send');                    // a bare phrase: nothing to send
  voice.voiceChat = false;
  assert.deepStrictEqual(sent, ['rotate it a little']);
  voice.voiceChat = true;
  engine.hear('half a');
  engine.fail('network');                 // a platform failure never fires a half-heard turn
  assert.deepStrictEqual(sent, ['rotate it a little']);
  const t = target();
  voice.startComposer(t);
  engine.hear('keep me in the box');
  voice.stopComposer(t);
  assert.deepStrictEqual(t.submits, [], 'a composer stop leaves the words in the textarea');
  assert.deepStrictEqual(t.texts.at(-1), 'keep me in the box');
});

test('voice chat serializes utterances behind a slow turn; a failed turn breaks neither chain nor mode', async () => {
  const m = make({ settings: { silenceMs: 500 } });
  const { voice, engine, clock, sent } = m;
  voice.voiceChat = true;
  engine.hear('first one send');
  assert.deepStrictEqual(sent, ['first one']);
  engine.hear('second one execute it');
  await Promise.resolve();
  assert.deepStrictEqual(sent, ['first one'], 'the second waits for the first turn');
  m.pending[0].reject(new Error('provider down'));
  await Promise.resolve(); await Promise.resolve(); await Promise.resolve();
  assert.deepStrictEqual(sent, ['first one', 'second one']);
  assert.strictEqual(voice.voiceChat, true);
  assert.strictEqual(m.pending.at(-1).meta.reason, 'phrase');
  engine.hear('third');
  clock.advance(500);
  await m.settle();
  await Promise.resolve();
  assert.deepStrictEqual(sent, ['first one', 'second one', 'third']);
  assert.strictEqual(m.pending.at(-1).meta.reason, 'silence');
});

test('a platform that refuses synchronously during start leaves startComposer false and the mode off', () => {
  const { voice, engine, notes, target } = make();
  engine.start = (s) => { engine.calls.push(['start', s.lang]); engine.session = s; s.onState?.('idle', { error: 'not-allowed' }); s.onError?.({ code: 'not-allowed', fatal: true, text: 'denied' }); return true; };
  const t = target();
  assert.strictEqual(voice.startComposer(t), false);
  assert.strictEqual(voice.mode, 'off');
  assert.deepStrictEqual(t.stops, ['error']);
  assert.deepStrictEqual(notes, [['denied', 'fail']]);
  voice.voiceChat = true;
  assert.strictEqual(voice.voiceChat, false, 'the setter cannot hold a mode the platform refused');
});

test('a fatal engine error turns the mode off, toasts, and reports the error in the state event', () => {
  const { voice, engine, win, notes, target } = make();
  const t = target();
  voice.startComposer(t);
  engine.fail('not-allowed');
  assert.strictEqual(voice.mode, 'off');
  assert.deepStrictEqual(t.stops, ['error']);
  assert.deepStrictEqual(notes, [['boom not-allowed', 'fail']]);
  const last = win.events.filter((e) => typeof e === 'object').at(-1);
  assert.deepStrictEqual(last, { mode: 'off', listening: false, supported: true, reason: 'error', error: 'not-allowed' });
});

test('a language change while active hot-swaps once; silence changes apply on the next arm', () => {
  const { voice, engine, cfg, win, clock, sent } = make();
  voice.voiceChat = true;
  assert.deepStrictEqual(engine.calls, [['start', 'en-US']]);
  win.fire(VOICE_SETTINGS_EVENT);   // nothing changed
  assert.strictEqual(engine.calls.length, 1);
  cfg.language = 'de-DE';
  win.fire(VOICE_SETTINGS_EVENT);
  assert.deepStrictEqual(engine.calls, [['start', 'en-US'], ['start', 'de-DE']]);
  cfg.silenceMs = 3000;
  engine.hear('warten');
  assert.deepStrictEqual(clock.pendingDelays, [3000]);
  clock.advance(3000);
  assert.deepStrictEqual(sent, ['warten']);
  voice.dispose();
  assert.strictEqual(voice.mode, 'off');
  cfg.language = 'fr-FR';
  win.fire(VOICE_SETTINGS_EVENT);   // unsubscribed
  assert.strictEqual(engine.calls.length, 3);
});

test('level subscribers hear every frame and can unsubscribe', () => {
  const { voice, engine } = make();
  const seen = [];
  const off = voice.onLevel((l) => seen.push(l));
  voice.voiceChat = true;
  engine.level(0.3);
  assert.strictEqual(voice.level, 0.3);
  off();
  engine.level(0.6);
  assert.deepStrictEqual(seen, [0.3]);
  assert.strictEqual(voice.level, 0.6);
});

// The mic's ring lives OUTSIDE its tile, but layout.css clips every button (the glass sweep)
// and a z-index:-1 pseudo on a non-isolated button paints behind the toolbar — so the ring
// never showed on either mic. The listening states must lift both, and drop the sweep whose
// clip they gave up; the ring's radius and the tile's glow follow --voice-level.
test('a listening mic uncovers its ray ring: no overflow clip, isolated stacking, no glass sweep, level-sized', async () => {
  const { readFileSync } = await import('node:fs');
  const css = readFileSync(new URL('../css/animations.css', import.meta.url), 'utf8');
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

// A spoken answer that lands behind a closed panel is announced by its toast alone: the
// hands-free chat never leaves the unread dot on the chat icon (user report — every
// answer nagged them to open a panel they had not asked for).
test('voice-chat answers behind a closed panel toast but never mark the chat icon unread', async () => {
  const { readFileSync } = await import('node:fs');
  const src = readFileSync(new URL('../js/llm/voiceModes.js', import.meta.url), 'utf8');
  const install = src.slice(src.indexOf('export const installVoiceModes'));
  assert.ok(install.includes('appNotify(toast.text, toast.type, { onClick: () => app.chat?.open?.() });'));
  // (There is no unread dot on either surface any more — an answer that lands while the
  // chat is away toasts, and the toast opens it. The voice path never marked one anyway.)
  assert.ok(!install.includes('markUnread'), 'the voice path marks nothing on the icon');
});

// …but an answer that ASKS something back (§11 ask card: options to pick) opens the
// panel instead of toasting: a card cannot be answered from a balloon, and hands-free
// there is nothing to tell the user a question is waiting out of sight.
test('a voice answer that asks a question opens the chat; a plain one still just toasts', async () => {
  const { readFileSync } = await import('node:fs');
  const src = readFileSync(new URL('../js/llm/voiceModes.js', import.meta.url), 'utf8');
  const onResult = src.slice(src.indexOf('onResult: (res) => {'), src.indexOf('const voice = createVoiceModes'));
  assert.ok(onResult.includes('res?.ok && res.entry?.ask && !surfaceOpen()'),
    'the ask card is what opens the panel');
  assert.ok(onResult.indexOf('app.chat?.open?.();') < onResult.indexOf('closedTurnToast(res)'),
    'and it opens BEFORE the toast path, which then does not fire');
  assert.ok(onResult.includes('const toast = closedTurnToast(res);'), 'a plain answer still toasts');
});

// The logo's shine is its own hover's (and its accent popover's) — switching the mic on
// must not light it (user report). Only the two mic faces wear the voice shine.
test('activating the microphone never latches the logo shine', async () => {
  const { readFileSync } = await import('node:fs');
  const toolbar = readFileSync(new URL('../js/ui/toolbar.js', import.meta.url), 'utf8');
  const css = readFileSync(new URL('../css/animations.css', import.meta.url), 'utf8');
  assert.ok(!toolbar.includes('voice-live'), 'no voice latch on the logo wrap');
  assert.ok(!css.includes('voice-live'), 'no voice rule targets the logo');
  const dust = readFileSync(new URL('../js/ui/voiceDust.js', import.meta.url), 'utf8');
  assert.ok(dust.includes('ringAngles(now)'), 'the mics keep their ring — on the dust canvas');
});

// The composer sends ONLY on the spoken phrase. A pause ends the dictation instead: the
// mic goes off with reason 'silence' and the words stay in the textarea to finish or send
// by hand. Either way the button keeps its (paused) mic face — chatView.js. Voice chat
// keeps sending on a pause.
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
