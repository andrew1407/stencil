import { replayWaves } from './motion.js';
import { notify } from '../utils.js';
import { VOICE_STATE_EVENT } from '../llm/voiceModes.js';
import { attachVoiceDust } from './voiceDust.js';
import { subscribe } from '../bus/appBus.js';
// The hands-free voice chat toggle (js/llm/voiceModes.js): `--voice-level` on <html>
// carries the live loudness (css/animations/voice.css sizes the mics' shine from it), and
// .active marks this button while voice chat is on — mirrored onto the fullscreen
// toolbar clone like the chat button's own state. The LOGO is not a wearer: its shine
// is its own hover (and its accent popover's), never the microphone's.
export function wireVoiceChatToggle(btn, app) {
  if (!btn || !app) return;
  const voice = () => app.voice;
  const buttons = () => document.querySelectorAll('#voice-chat-btn');
  let wasOn = false;
  const sync = () => {
    const v = voice();
    const on = !!v?.voiceChat;
    for (const el of buttons()) {
      el.classList.toggle('active', on);
      if (on !== wasOn) replayWaves(el, on);   // the waves swell in / fly out
    }
    wasOn = on;
  };
  if (!voice()?.supported) btn.disabled = true;   // the disabled-reason tooltip says why
  // Motes leave the tile with the voice while it listens (ui/voiceDust.js).
  attachVoiceDust(btn, () => btn.classList.contains('active'));
  btn.addEventListener('click', () => {
    const v = voice();
    if (!v) return;
    try { v.voiceChat = !v.voiceChat; } catch (err) { notify(err?.message || String(err), 'fail'); }
    sync();
  });
  voice()?.onLevel((level) => {
    document.documentElement.style.setProperty('--voice-level', level.toFixed(3));
  });
  subscribe(VOICE_STATE_EVENT, sync);
  sync();
}

// Double-click (or double-tap) the logo opens a native colour picker that tints THIS
