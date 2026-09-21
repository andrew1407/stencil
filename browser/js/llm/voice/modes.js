// Voice input modes — the one coordinator behind every way of talking to the assistant.
// 'composer' dictates into a chat textarea: only a spoken "send / execute" sends, and a pause
// longer than the silence setting ends the dictation with the words left in the box.
// 'chat' is the toolbar's hands-free voice chat: it listens with the chat closed and each
// utterance becomes a logged turn. One mode listens at a time; every clock is injected.
import { createVoiceInput, voiceErrorText } from './input.js';
import { loadVoiceSettings, recognitionLang, VOICE_SETTINGS_EVENT } from './settings.js';
import { loadLlmSettings } from '../settings.js';
import { sharedChatController, runLoggedChatTurn, closedTurnToast, spokenEcho } from '../chat/session.js';
import { notify as appNotify } from '../../utils.js';
import { publish, subscribe, EVENTS } from '../../eventBus/appBus.js';

export const VOICE_STATE_EVENT = EVENTS.voiceStateChanged;
export const VOICE_ACTIVE_LEVEL = 0.2;   // speech reads 0.4–1.0, room noise stays under 0.1
export const UNSUPPORTED_TEXT = 'Voice input is not supported in this browser';

// The spoken "send" — only as the WHOLE tail of an utterance, longest phrase first so "send
// it" is never read as "send" plus a dangling "it". Trailing punctuation is tolerated.
export const SEND_PHRASES = Object.freeze(['send it', 'execute it', 'send', 'execute']);
const SEND_RE = new RegExp(`(^|[\\s,.;:!?])(${SEND_PHRASES.join('|')})[\\s.,;:!?]*$`, 'i');
export const splitSendPhrase = (raw) => {
  const text = String(raw ?? '').replace(/\s+/g, ' ').trim();
  const m = SEND_RE.exec(text);
  if (!m) return { text, send: false, phrase: null };
  const head = text.slice(0, m.index).replace(/[\s.,;:!?]+$/, '').trim();
  return { text: head, send: true, phrase: m[2].toLowerCase() };
};

export const createVoiceModes = ({
  engine = createVoiceInput(),
  sendTurn = async () => {},
  loadSettings = loadVoiceSettings,
  setTimer = (fn, ms) => setTimeout(fn, ms),
  clearTimer = (id) => clearTimeout(id),
  now = () => Date.now(),
  notify = null,
  win = (typeof window !== 'undefined' ? window : null),
} = {}) => {
  let mode = 'off';            // 'off' | 'composer' | 'chat'
  let target = null;           // composer: { setText(text), submit(), onStop(reason), hasText?() }
  let lang = null;
  let timer = null;
  let lastLoudAt = -Infinity;
  let lastResultAt = -Infinity;
  let level = 0;
  const sendQueue = [];        // voice-chat utterances waiting for the shared conversation
  let sendBusy = false;
  const levelSubs = new Set();

  const emit = (extra) => {
    publish(VOICE_STATE_EVENT, { mode, listening: mode !== 'off' && engine.listening, supported: engine.supported, ...extra },
            { target: win });
  };

  // One silence timer, armed by results; loudness only leaves a timestamp, so the 60 Hz level
  // stream never churns timers. Recent loud audio extends the wait, capped at twice the setting.
  const silenceMs = () => loadSettings().silenceMs;
  const clearSilence = () => { if (timer !== null) { clearTimer(timer); timer = null; } };
  const arm = (ms) => { clearSilence(); timer = setTimer(tick, ms); };
  const tick = () => {
    timer = null;
    const ms = silenceMs();
    const t = now();
    const sinceLoud = t - lastLoudAt;
    const sinceResult = t - lastResultAt;
    if (sinceLoud < ms && sinceResult < 2 * ms) {
      arm(Math.max(50, Math.min(ms - sinceLoud, 2 * ms - sinceResult)));
      return;
    }
    flush('silence');
  };

  // The utterance is done: hand it on, and start the next one from a clean slate.
  const flush = (reason) => {
    const { text } = splitSendPhrase(engine.transcript.text);
    engine.commit();
    clearSilence();
    // A composer's pause is an END, not a send: the mic goes off, the words already
    // stand in the textarea. (The face stays a paused mic either way — view.js.)
    if (mode === 'composer' && reason === 'silence') { leave('silence'); return; }
    if (mode === 'composer') {
      // A bare "send it" is a command about what is ALREADY in the box, but over an EMPTY one it
      // ends nothing: a stray "send" across a quiet room must not close a live dictation.
      if (!text && !target?.hasText?.()) return;
      if (text) target?.setText(text);   // …and a bare send never rewrites the box
      target?.submit(reason);
      // One utterance, one message: hands-free chaining is the toolbar's voice CHAT mode.
      leave(reason);
    } else if (mode === 'chat' && text) {
      // Chat mode has no box behind it, so a bare "send" there really has nothing to say.
      sendQueue.push({ text, reason });
      pumpSends();
    }
  };
  // Serialized: one logged turn at a time on the shared conversation. The first send
  // goes out synchronously (its toast shows at once); a failed turn never stalls the rest.
  const pumpSends = () => {
    if (sendBusy || !sendQueue.length) return;
    sendBusy = true;
    const { text, reason } = sendQueue.shift();
    let p;
    try { p = Promise.resolve(sendTurn(text, { reason })); } catch { p = Promise.resolve(); }
    p.catch(() => {}).finally(() => { sendBusy = false; pumpSends(); });
  };

  const onTranscript = (t) => {
    lastResultAt = now();
    const { text, send } = splitSendPhrase(t.text);
    if (mode === 'composer') target?.setText(text);
    if (send) { flush('phrase'); return; }
    if (text) arm(silenceMs()); else clearSilence();
  };
  const onLevel = (l) => {
    level = l;
    if (l > VOICE_ACTIVE_LEVEL) lastLoudAt = now();
    for (const fn of levelSubs) { try { fn(l); } catch { /* a listener's problem */ } }
  };
  const onState = () => emit();
  const onError = (err) => {
    if (!err?.fatal) return;
    leave('error', err);
    notify?.(err.text || voiceErrorText(err.code), 'fail');
  };
  const session = () => ({ lang, onTranscript, onLevel, onState, onError });

  const enter = (m, tgt) => {
    if (!engine.supported) throw new Error(UNSUPPORTED_TEXT);
    if (m === mode && tgt === target) return;
    const prev = target;
    const prevMode = mode;
    clearSilence();
    if (prevMode !== 'off') engine.commit();
    mode = m;
    target = tgt || null;
    lang = recognitionLang(loadSettings().language);
    if (prevMode !== 'off') prev?.onStop?.('switch');
    engine.start(session());
    emit();
  };
  const leave = (reason = 'stop', err = null) => {
    if (mode === 'off') return;
    // Switching voice chat off is not a cancel: whatever was said and not yet sent goes
    // out now (a composer keeps its words in the box instead — nothing to do there).
    if (mode === 'chat' && reason !== 'error') flush('stop');
    clearSilence();
    engine.commit();
    const prev = target;
    mode = 'off';
    target = null;
    engine.stop();
    prev?.onStop?.(reason);
    emit(err ? { reason, error: err.code } : { reason });
  };
  const onSettings = () => {
    if (mode === 'off') return;
    const next = recognitionLang(loadSettings().language);
    if (next === lang) return;
    lang = next;
    engine.start(session());
  };
  const offSettings = subscribe(VOICE_SETTINGS_EVENT, onSettings, { target: win });

  return {
    get supported() { return engine.supported; },
    get mode() { return mode; },
    get listening() { return mode !== 'off' && engine.listening; },
    get level() { return level; },
    get target() { return target; },
    get voiceChat() { return mode === 'chat'; },
    set voiceChat(on) {
      if (on) enter('chat', null);
      else if (mode === 'chat') leave();
    },
    // False (no throw) when the browser cannot listen. True only if the composer really holds the
    // mic: a platform that refuses synchronously has already fired the fatal path by then.
    startComposer(tgt) {
      if (!engine.supported) return false;
      enter('composer', tgt);
      return mode === 'composer' && target === tgt;
    },
    stopComposer(tgt) {
      if (mode !== 'composer') return;
      if (tgt && tgt !== target) return;
      leave();
    },
    toggleComposer(tgt) {
      if (mode === 'composer' && target === tgt) { leave(); return false; }
      return this.startComposer(tgt);
    },
    stopAll(reason) { leave(reason); },
    onLevel(fn) { levelSubs.add(fn); return () => levelSubs.delete(fn); },
    dispose() {
      leave();
      offSettings();
      levelSubs.clear();
    },
  };
};

// Voice-chat turns go through the SAME logged-turn frame the composers use (shared history,
// transcript rows). Deliberately no unread dot — hands-free means nothing to dismiss.
export const installVoiceModes = (app, over = {}) => {
  const surfaceOpen = () => !!(app.chat?.isOpen?.() || app.assistantFlyoutOpen?.());
  const sendTurn = over.sendTurn || ((text) => {
    // Quoted, not dashed: the balloon is REPEATING what the mic heard, and quotes say
    // that at a glance where a dash read as prose ("Sent: \"make it sepia\"").
    if (!surfaceOpen()) appNotify(`Sent: "${spokenEcho(text)}"`, 'info', { key: 'voice-sent' });
    return runLoggedChatTurn(sharedChatController(app), text, {
      settings: loadLlmSettings(),
      onResult: (res) => {
        // §11: an answer that ASKS something back is a card with options, and it cannot be answered
        // from a toast — so the panel opens itself for a question, and only for a question.
        if (res?.ok && res.entry?.ask && !surfaceOpen()) {
          try { app.chat?.open?.(); } catch { /* no panel on this surface */ }
          return;
        }
        const toast = closedTurnToast(res);
        if (surfaceOpen() || !toast) return;
        appNotify(toast.text, toast.type, { onClick: () => app.chat?.open?.() });
      },
    });
  });
  const voice = createVoiceModes({ notify: appNotify, ...over, sendTurn });
  app.voice = voice;
  return voice;
};
