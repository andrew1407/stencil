// Shared rig for the voiceModes specs: an engine double, a window double and the
// createVoiceModes factory wired to a stub clock. Extracted from voiceModes.test.js.
import { createVoiceModes } from '../../js/llm/voice/voiceModes.js';
import { stubClock } from './speech.js';

// An engine double with the real engine's surface: the test drives transcripts,
// levels and fatal errors through the session it was last started with.
export const fakeEngine = ({ supported = true } = {}) => {
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
export const fakeWindow = () => {
  const listeners = new Map();
  return {
    events: [],
    addEventListener(type, fn) { listeners.set(type, fn); },
    removeEventListener(type) { listeners.delete(type); },
    dispatchEvent(ev) { this.events.push(ev.detail ?? ev.type); listeners.get(ev.type)?.(ev); return true; },
    fire(type) { listeners.get(type)?.({ type }); },
  };
};
export const make = ({ supported = true, settings = {} } = {}) => {
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
