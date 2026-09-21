// Voice input's only knob in this dialog: Send-after-pause (ms). Language is fixed to
// English — change it only via stencil.voiceInputLanguage (console/settingsFacade.js).
import { loadVoiceSettings, saveVoiceSettings, clampSilenceMs, SILENCE_MS_DEFAULT } from '../../llm/voice/voiceSettings.js';

export const wireVoiceSilenceRow = () => {
  const el = document.getElementById('vs-voice-silence');
  el.addEventListener('change', e => {
    e.target.value = clampSilenceMs(e.target.value);
    saveVoiceSettings({ ...loadVoiceSettings(), silenceMs: e.target.value });
  });
  return {
    populate: () => { el.value = loadVoiceSettings().silenceMs; },
    reset: () => saveVoiceSettings({ ...loadVoiceSettings(), silenceMs: SILENCE_MS_DEFAULT }),
  };
};
