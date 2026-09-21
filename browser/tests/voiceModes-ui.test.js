// The listening mic's own decoration and the hands-free surface: the ray ring, the toast that
// never marks the chat unread, the unlatched logo and a dictation pause. From voiceModes.test.js.
import { test } from 'node:test';
import assert from 'node:assert';
import { ANIMATIONS_CSS } from './helpers/css.js';
import { make } from './helpers/voiceModesRig.js';

test('a listening mic uncovers its ray ring: no overflow clip, isolated stacking, no glass sweep, level-sized', async () => {
  const { readFileSync } = await import('node:fs');
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
  const { readFileSync } = await import('node:fs');
  const src = readFileSync(new URL('../js/llm/voiceModes.js', import.meta.url), 'utf8');
  const install = src.slice(src.indexOf('export const installVoiceModes'));
  assert.ok(install.includes('appNotify(toast.text, toast.type, { onClick: () => app.chat?.open?.() });'));
  // (There is no unread dot on either surface any more — an answer that lands while the
  // chat is away toasts, and the toast opens it. The voice path never marked one anyway.)
  assert.ok(!install.includes('markUnread'), 'the voice path marks nothing on the icon');
});

// …but an answer that ASKS something back (a §11 ask card) opens the panel instead of toasting: a
// card cannot be answered from a balloon.
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
  const toolbar = readFileSync(new URL('../js/ui/toolbar/toolbar.js', import.meta.url), 'utf8');
  const css = ANIMATIONS_CSS;
  assert.ok(!toolbar.includes('voice-live'), 'no voice latch on the logo wrap');
  assert.ok(!css.includes('voice-live'), 'no voice rule targets the logo');
  const dust = readFileSync(new URL('../js/ui/dust/voiceDust.js', import.meta.url), 'utf8');
  assert.ok(dust.includes('ringAngles(now)'), 'the mics keep their ring — on the dust canvas');
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
