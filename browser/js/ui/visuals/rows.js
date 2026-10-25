// The Visuals window's small rows: a speed row (number box + slider over one motion pref) and voice
// input's one knob here, Send-after-pause (ms). Language is fixed to English — change it only via
// stencil.voiceInputLanguage (console/settingsFacade.js).
import { loadVoiceSettings, saveVoiceSettings, clampSilenceMs, SILENCE_MS_DEFAULT } from '../../llm/voice/settings.js';

// The slider applies live and the box on change, both through the shared motion setter.
export const wireSpeedRow = ({ range, number, key, app }) => {
  // The bar fills to its thumb (the scrub's --scrub-fill).
  const paint = (v) => {
    range.value = v;
    number.value = Number(v).toFixed(2);
    range.style.setProperty('--scrub-fill', `${((v - range.min) / (range.max - range.min)) * 100}%`);
  };
  range.addEventListener('input', () => paint(app.settings.setMotion(key, range.value)[key]));
  number.addEventListener('change', () => paint(app.settings.setMotion(key, number.value)[key]));
  return { paint };
};

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
