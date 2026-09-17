// One composer's voice input: the mic face on the Send button (js/llm/voiceModes.js).
import { UNSUPPORTED_TEXT, VOICE_STATE_EVENT } from '../llm/voiceModes.js';
import { icon } from './icons.js';
import { notify } from '../utils.js';
import { swapContent } from './motion.js';
import { subscribe } from '../eventBus/appBus.js';

// Two layers of state: the FACE (`on`, flipped by the "…" item, a double-click or a hold)
// and LISTENING (the coordinator's, with this surface's target). Dictation lands after
// whatever was typed; the auto-send goes through the surface's own `send`.
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
// Anything to send without this utterance? A spoken "send it" over a full box is a
// command about exactly that (voiceModes.js flush).
    hasText: () => !!input.value.trim(),
// Ending an utterance ends listening, never the mode; only a fatal error returns the send plane.
    onStop: (reason) => { if (reason === 'error') setFace(false); else sync(); },
  };
  const isListening = () => !!voice() && voice().mode === 'composer' && voice().target === target;
// The "…" item names the other mode, glyph and all.
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
// Switching to voice only changes the face (paused mic); switching back stops dictation.
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
// Record the item's first face so the very first switch turns in place too.
  if (item) swapContent(item, item.innerHTML, { key: 'mic' });
  if (item && !supported()) {
    item.disabled = true;
    item.dataset.title = UNSUPPORTED_TEXT;
  }
// The engine's own transitions repaint the face; the toolbar's hands-free voice chat does
// not move the Send button under the user's finger.
  subscribe(VOICE_STATE_EVENT, () => sync(), { target: win });
  return {
    isOn: () => on,
    isListening,
    toggleMode,
    toggleListening,
    state: () => (on ? { on: true, listening: isListening() } : null),
    supported,
    setMode: (next) => { if (!!next !== on) toggleMode(); },
    target,
  };
};
