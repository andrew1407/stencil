// Voice input settings: how long a pause ends an utterance, and which language the
// recognizer listens for. Persisted under their own key so the §5 blob keeps the
// contract's shape; saving publishes VOICE_SETTINGS_EVENT so a live recognizer re-arms.

export interface VoiceSettings {
  /** Milliseconds of silence that end an utterance, within SILENCE_MS_MIN..MAX. */
  silenceMs: number;
  /** 'default' (the recognizer's English) or any BCP-47 tag as typed. */
  language: string;
}

export declare const VOICE_SETTINGS_EVENT: string;
export declare const SILENCE_MS_DEFAULT: number;
export declare const SILENCE_MS_MIN: number;
export declare const SILENCE_MS_MAX: number;
/** The settings dialog's menu as [tag, label] pairs; the facade accepts any tag. */
export declare const VOICE_LANGUAGES: ReadonlyArray<readonly [string, string]>;
export declare const isLanguageTag: (v: unknown) => boolean;
export declare const clampSilenceMs: (v: unknown) => number;
export declare const normalizeLanguage: (v: unknown) => string;
/** What the recognizer is told: the tag, or 'en-US' for 'default'. */
export declare const recognitionLang: (language: unknown) => string;
export declare const defaultVoiceSettings: () => VoiceSettings;
export declare const loadVoiceSettings: () => VoiceSettings;
export declare const saveVoiceSettings: (s: Partial<VoiceSettings> | null | undefined) => void;
