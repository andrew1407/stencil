// ── One composer's voice input ──────────────────────────────────
// The mic face on a composer's Send button (js/llm/voiceModes.js), shared by both surfaces.
import { UNSUPPORTED_TEXT, VOICE_STATE_EVENT } from '../llm/voiceModes.js';
import { icon } from './icons.js';
import { notify } from '../utils.js';
import { swapContent } from './motion.js';
import { subscribe } from '../bus/appBus.js';

// One composer's voice input (js/llm/voiceModes.js), the same for the panel and the
// flyout. Two layers of state: the FACE (`on` — the mic glyph instead of Send, flipped
// by the "…" item, a double-click or a hold) and LISTENING (the coordinator's, this
// surface's target being the one it dictates into — paused by a click or Escape,
// resumed by a click). Dictation lands in the textarea after whatever was already
// typed; the auto-send goes through the surface's own `send`, so history, attachments
// and the Stop face behave exactly as for a typed message.
//   { prefix, input, sendBtn, doc, app, send, sync }  →  { isOn, isListening, toggleMode, toggleListening, state, setMode }
export const wireComposerVoice = ({ prefix, input, sendBtn, doc = document, app, send, sync, win = (typeof window !== 'undefined' ? window : null) }) => {
  const item = doc.getElementById(`${prefix}-voice`);
  const voice = () => app.voice;
  const supported = () => !!voice()?.supported;
  let on = false;
  let typedPrefix = '';
  const target = {
    setText: (t) => {
      input.value = typedPrefix ? `${typedPrefix} ${t}`.trimEnd() : t;
      sync();
    },
    submit: () => { typedPrefix = ''; send(); },
    // Whether there is anything to send WITHOUT this utterance: what was typed before the
    // mic went on, or an earlier utterance a pause left standing here. A spoken "send it"
    // over a full box is a command about exactly that (js/llm/voiceModes.js flush).
    hasText: () => !!input.value.trim(),
    // Ending an utterance ends LISTENING, never the mode — whatever ended it (a pause,
    // a spoken "send", another surface taking the mic): the face stays a paused mic and a
    // click resumes it. Only a FATAL error returns the send plane; there is no mic left.
    onStop: (reason) => { if (reason === 'error') setFace(false); else sync(); },
  };
  const isListening = () => !!voice() && voice().mode === 'composer' && voice().target === target;
  // The "…" item names the OTHER mode, glyph and all: a mic to switch to dictation, the
  // send plane to switch back (the same in-place swap the button's face uses).
  const setFace = (next) => {
    on = next;
    if (item) {
      swapContent(item, `${icon(next ? 'send' : 'mic', { size: 14 })}<span>${next ? 'Use send button' : 'Use voice input'}</span>`,
        { key: next ? 'send' : 'mic' });
    }
    sync();
  };
  const startListening = () => {
    if (!supported()) { notify(UNSUPPORTED_TEXT, 'fail'); return false; }
    typedPrefix = input.value.trim();
    return voice().startComposer(target);
  };
  const stopListening = () => voice()?.stopComposer(target);
  // Switching TO voice input only changes the face: the mic shows PAUSED and a click on it
  // starts listening, so the switch gesture alone never opens the mic. Switching back
  // stops any dictation and returns the send plane.
  const toggleMode = () => {
    if (on) { stopListening(); setFace(false); input.focus?.(); return; }
    if (!supported()) { notify(UNSUPPORTED_TEXT, 'fail'); return; }
    setFace(true);
  };
  const toggleListening = () => {
    if (isListening()) stopListening(); else if (!startListening()) return;
    sync();
  };
  item?.addEventListener('click', toggleMode);
  // Record the item's first face so the very first switch turns in place too (the
  // shared swap treats an unknown element's first write as a plain paint).
  if (item) swapContent(item, item.innerHTML, { key: 'mic' });
  if (item && !supported()) {
    item.disabled = true;
    item.dataset.title = UNSUPPORTED_TEXT;
  }
  // The engine's own transitions (starting → listening, a fatal stop) repaint the face.
  // The toolbar's hands-free voice chat does NOT: this face is the user's own choice here,
  // so the toolbar mic must never move the Send button under their finger. One mode still
  // listens at a time; only the FACE stays put.
  subscribe(VOICE_STATE_EVENT, () => sync(), { target: win });
  return {
    isOn: () => on,
    isListening,
    toggleMode,
    toggleListening,
    // For syncComposerControls.
    state: () => (on ? { on: true, listening: isListening() } : null),
    supported,
    // Scripting: put the composer in (or out of) voice mode outright.
    setMode: (next) => { if (!!next !== on) toggleMode(); },
    target,
  };
};
