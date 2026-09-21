// ── Voice input settings ───────────────────────────────────────────────────
// The two knobs speech-to-text exposes (voiceModes.js): how long a pause ends an utterance,
// and which language the recognizer listens for. Persisted under their own key so the §5
// blob keeps the contract's shape; every localStorage access is guarded for Node.
import { clamp } from '../../utils/math.js';
import { publish, EVENTS } from '../../eventBus/appBus.js';

const VOICE_SETTINGS_KEY = 'drawingApp_voiceSettings';
export const VOICE_SETTINGS_EVENT = EVENTS.voiceSettingsChanged;

export const SILENCE_MS_DEFAULT = 1000;
export const SILENCE_MS_MIN = 500;
export const SILENCE_MS_MAX = 10_000;
// 'default' = the recognizer's English; any BCP-47 tag is accepted (en-GB, uk-UA, …).
const DEFAULT_LANGUAGE = 'default';
const DEFAULT_RECOGNITION_LANG = 'en-US';

// The settings dialog's menu — a short, common set; the facade takes any tag.
export const VOICE_LANGUAGES = Object.freeze([
  ['default', 'Default (English)'],
  ['en-US', 'English (US)'],
  ['en-GB', 'English (UK)'],
  ['de-DE', 'Deutsch'],
  ['fr-FR', 'Français'],
  ['es-ES', 'Español'],
  ['it-IT', 'Italiano'],
  ['pt-BR', 'Português (Brasil)'],
  ['pl-PL', 'Polski'],
  ['uk-UA', 'Українська'],
  ['ru-RU', 'Русский'],
  ['ja-JP', '日本語'],
  ['zh-CN', '中文 (简体)'],
]);

const ls = () => (typeof localStorage !== 'undefined' ? localStorage : null);

// A BCP-47-looking tag: 2–3 letter language, optional 2–8 char subtags (zh-Hans-CN).
const LANG_TAG_RE = /^[a-z]{2,3}(-[a-z0-9]{2,8})*$/i;
export const isLanguageTag = (v) => typeof v === 'string' && LANG_TAG_RE.test(v.trim());

export const clampSilenceMs = (v) => {
  const n = Number(v);
  if (!Number.isFinite(n)) return SILENCE_MS_DEFAULT;
  return clamp(Math.round(n), SILENCE_MS_MIN, SILENCE_MS_MAX);
};

// '' / 'default' → 'default'; a tag stays as typed (trimmed); anything else → 'default'.
export const normalizeLanguage = (v) => {
  const s = String(v ?? '').trim();
  if (s === '' || s.toLowerCase() === DEFAULT_LANGUAGE) return DEFAULT_LANGUAGE;
  return LANG_TAG_RE.test(s) ? s : DEFAULT_LANGUAGE;
};

// What the recognizer is told: the setting's tag, or English for 'default'.
export const recognitionLang = (language) =>
  (normalizeLanguage(language) === DEFAULT_LANGUAGE ? DEFAULT_RECOGNITION_LANG : normalizeLanguage(language));

export const defaultVoiceSettings = () => ({ silenceMs: SILENCE_MS_DEFAULT, language: DEFAULT_LANGUAGE });

// Saved overrides merged over the defaults. Bad/missing data degrades to defaults.
export const loadVoiceSettings = () => {
  const out = defaultVoiceSettings();
  try {
    const raw = ls()?.getItem(VOICE_SETTINGS_KEY);
    const saved = raw ? JSON.parse(raw) : null;
    if (saved && typeof saved === 'object') {
      if (saved.silenceMs !== undefined) out.silenceMs = clampSilenceMs(saved.silenceMs);
      if (typeof saved.language === 'string') out.language = normalizeLanguage(saved.language);
    }
  } catch {
    /* storage blocked / corrupt — fall back to the defaults */
  }
  return out;
};

// Writes the slim shape and tells every listener (the coordinator swaps the
// recognizer's language live) — the dispatch lives HERE so no caller can forget it.
export const saveVoiceSettings = (s) => {
  try {
    const slim = { silenceMs: clampSilenceMs(s?.silenceMs), language: normalizeLanguage(s?.language) };
    ls()?.setItem(VOICE_SETTINGS_KEY, JSON.stringify(slim));
  } catch {
    /* storage blocked — settings live for this session only */
  }
  publish(VOICE_SETTINGS_EVENT);
};
