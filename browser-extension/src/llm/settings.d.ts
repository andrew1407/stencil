// Shapes for llm/settings.js — the stored provider configuration (contract §5).
// The endpoint is always explicit user configuration, never discovered from content.

/** Contract §5 providers, plus the extension's own off-switch. */
export type LlmProvider = 'none' | 'ollama' | 'openai-compat' | 'stencil-server';

export interface LlmSettings {
  provider: LlmProvider;
  /** ollama / openai-compat endpoint. */
  baseUrl: string;
  model: string;
  /** openai-compat bearer key; never sent to any other provider. */
  apiKey: string;
  /** stencil-server endpoint, and the token used when no stored connection matches. */
  serverUrl: string;
  serverToken: string;
  /** May the assistant see the open source tabs (contract §8)? */
  shareTabs: boolean;
}

export declare const LLM_SETTINGS_KEY: string;
export declare const PROVIDERS: LlmProvider[];
export declare const PROVIDER_BASE_URLS: Record<string, string>;
export declare const URL_KEYS: string[];
export declare function assistantEnabled(settings: LlmSettings | null | undefined): boolean;
export declare function isHttpUrl(value: unknown): boolean;
export declare function defaultSettings(connections?: unknown[]): LlmSettings;
export declare function loadLlmSettings(opts?: { connections?: unknown[] }): Promise<LlmSettings>;
export declare function saveLlmSettings(settings?: Partial<LlmSettings>): Promise<LlmSettings>;
