// ── Speech recognition engine ──────────────────────────────────────────────
// One continuous Web Speech `SpeechRecognition` session that survives the browser's own
// end-of-utterance stops (we restart from `onend` while the user still wants to listen),
// plus the level meter (voiceLevel.js). No send policy here: voiceModes.js decides WHEN an
// utterance is done, this says WHAT has been heard since the last commit(). The recognizer,
// timers and meter are injected for `node --test` (tests/helpers/speech.js).
import { createLevelMeter as realLevelMeter } from './voiceLevel.js';

export const isVoiceSupported = (win = globalThis) =>
  !!(win && (win.SpeechRecognition || win.webkitSpeechRecognition));

const defaultRecognizer = () =>
  (typeof window !== 'undefined' ? (window.SpeechRecognition || window.webkitSpeechRecognition) : undefined);

// Errors after which listening cannot continue (the rest are transient and restart).
export const FATAL_ERRORS = new Set([
  'not-allowed', 'service-not-allowed', 'audio-capture', 'language-not-supported', 'bad-grammar',
  'start-failed',
]);
export const NETWORK_BACKOFF_MS = Object.freeze([250, 1000, 2000]);   // three network drops in a row → fatal
// How long to wait before restarting after `fails` consecutive network drops. Anything
// but a network drop restarts at once; the table's last step is the floor.
const restartDelayMs = (code, fails) =>
  (code === 'network' ? NETWORK_BACKOFF_MS[Math.min(fails, NETWORK_BACKOFF_MS.length) - 1] || 0 : 0);

const ERROR_TEXT = {
  'not-allowed': 'Microphone access was denied — allow it for this site to use voice input.',
  'service-not-allowed': 'Speech recognition is blocked by the browser on this page.',
  'audio-capture': 'No microphone was found.',
  'language-not-supported': 'Speech recognition does not support the chosen language.',
  'bad-grammar': 'Speech recognition could not start (grammar error).',
  'start-failed': 'Speech recognition could not start.',
  network: 'Speech recognition lost its connection.',
};
export const voiceErrorText = (code) => ERROR_TEXT[code] || `Voice input stopped (${code || 'unknown error'}).`;

const norm = (s) => String(s || '').replace(/\s+/g, ' ').trim();
const joinResults = (results, from, wantFinal) => {
  let out = '';
  for (let i = from; i < (results?.length || 0); i++) {
    const r = results[i];
    if (!!r.isFinal !== wantFinal) continue;
    out += ` ${r[0]?.transcript ?? ''}`;
  }
  return out;
};

export const createVoiceInput = ({
  SpeechRecognition = defaultRecognizer(),
  createLevelMeter = realLevelMeter,
  setTimer = (fn, ms) => setTimeout(fn, ms),
  clearTimer = (id) => clearTimeout(id),
} = {}) => {
  const supported = typeof SpeechRecognition === 'function';
  let active = false;          // the caller's intent: keep listening
  let state = 'idle';          // 'idle' | 'starting' | 'listening' | 'stopping'
  let session = null;          // { lang, onTranscript, onLevel, onState, onError }
  let rec = null;              // the live recognizer instance (events from any other are ignored)
  let meter = null;
  let restartTimer = null;
  // Transcript bookkeeping across restarts: a restarted recognizer numbers its results
  // from 0 again, so what was already reported by a dead instance is `carried`, and
  // `commitIndex` marks how much of the LIVE instance the caller has consumed.
  let carried = '';
  let commitIndex = 0;
  let results = null;
  let lastError = null;
  let networkFails = 0;
  let startThrows = 0;

  const setState = (s, extra) => {
    if (state === s && !extra) return;
    state = s;
    session?.onState?.(s, extra);
  };
  const transcript = () => {
    const final = norm(`${carried} ${joinResults(results, commitIndex, true)}`);
    const interim = norm(joinResults(results, commitIndex, false));
    return { final, interim, text: norm(`${final} ${interim}`) };
  };
  const detach = (r) => {
    if (!r) return;
    r.onstart = r.onresult = r.onerror = r.onend = null;
    if (rec === r) rec = null;
  };
  const cancelRestart = () => { if (restartTimer !== null) { clearTimer(restartTimer); restartTimer = null; } };
  const stopMeter = () => { const m = meter; meter = null; try { m?.stop(); } catch { /* best effort */ } };

  const finish = (extra) => {
    cancelRestart();
    detach(rec);
    results = null;
    commitIndex = 0;
    stopMeter();
    setState('idle', extra);
  };
  const fatal = (code) => {
    active = false;
    const r = rec;
    detach(r);
    try { r?.abort(); } catch { /* already gone */ }
    carried = '';
    finish({ error: code });
    session?.onError?.({ code, fatal: true, text: voiceErrorText(code) });
  };
  const scheduleRestart = (ms) => {
    cancelRestart();
    restartTimer = setTimer(() => { restartTimer = null; if (active) startRec(); }, ms);
  };

  const startRec = () => {
    let r;
    try { r = new SpeechRecognition(); } catch { fatal('start-failed'); return; }
    r.continuous = true;
    r.interimResults = true;
    r.maxAlternatives = 1;
    r.lang = session.lang;
    r.onstart = () => { if (rec === r) setState('listening'); };
    r.onresult = (e) => {
      if (rec !== r) return;
      results = e.results;
      networkFails = 0;
      session?.onTranscript?.(transcript());
    };
    r.onerror = (e) => {
      if (rec !== r) return;
      const code = e?.error || 'unknown';
      if (FATAL_ERRORS.has(code)) { fatal(code); return; }
      if (code === 'network' && ++networkFails >= NETWORK_BACKOFF_MS.length) { fatal('network'); return; }
      lastError = code;   // the restart decision is onend's — it always follows
    };
    r.onend = () => {
      if (rec !== r) return;
      // Whatever this instance finalized and nobody consumed rides along to the next one.
      carried = norm(`${carried} ${joinResults(results, commitIndex, true)}`);
      results = null;
      commitIndex = 0;
      rec = null;
      if (!active) { finish(); return; }
      const err = lastError;
      lastError = null;
      setState('starting');
      scheduleRestart(restartDelayMs(err, networkFails));
    };
    rec = r;
    results = null;
    commitIndex = 0;
    lastError = null;
    setState('starting');
    try {
      r.start();
      startThrows = 0;
    } catch {
      // InvalidStateError: a stale instance is still winding down — drop this one and
      // try once more; twice in a row means the platform will not give us a session.
      detach(r);
      try { r.abort(); } catch { /* nothing to abort */ }
      if (++startThrows >= 2) { startThrows = 0; fatal('start-failed'); return; }
      scheduleRestart(0);
    }
  };

  return {
    get supported() { return supported; },
    get active() { return active; },
    get state() { return state; },
    get listening() { return state === 'listening'; },
    get transcript() { return transcript(); },
    // Begin (or, while active, HOT-SWAP) a session: the callbacks are replaced, what was
    // heard so far is committed, and only a changed `lang` recreates the recognizer —
    // the mic stays open across a mode switch. False when the browser cannot do this.
    start(next) {
      if (!supported) return false;
      const langChanged = active && session?.lang !== next.lang;
      if (active) {
        this.commit();
        session = { ...next };
        if (langChanged) {
          const r = rec;
          detach(r);
          try { r?.abort(); } catch { /* nothing to abort */ }
          cancelRestart();
          startRec();
        } else {
          session.onState?.(state);
        }
        return true;
      }
      active = true;
      session = { ...next };
      carried = '';
      networkFails = 0;
      startThrows = 0;
      meter = createLevelMeter();
      // The visuals are cosmetic: a refused mic costs the shine, not the words —
      // recognition itself reports `not-allowed` if the platform really said no.
      meter.start((lvl) => session?.onLevel?.(lvl)).catch(() => {});
      startRec();
      return true;
    },
    // Everything reported so far has been consumed — a later finalization of the
    // same result index is dropped with it, so a phrase caught on an interim result
    // can never be sent twice.
    commit() {
      carried = '';
      commitIndex = results?.length || 0;
    },
    // Graceful: pending finals still arrive, then the state settles to 'idle' with
    // no restart. Idempotent.
    stop() {
      if (!active) return;
      active = false;
      cancelRestart();
      const r = rec;
      if (!r) { finish(); return; }
      setState('stopping');
      try { r.stop(); } catch { detach(r); finish(); }
    },
  };
};
