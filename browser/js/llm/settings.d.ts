// LLM assistant settings (llm-contract.md §5): the persisted provider configuration in the
// contract's shape, plus the §12 chat-persistence opt-in. Endpoints are explicit user
// configuration and http(s) only; every localStorage access is guarded for Node.
import type { DrawingApp } from '../core/drawingApp.js';

/** Contract §5 providers, plus 'none' — the local-only "assistant switched off" value. */
export type LlmProvider = 'none' | 'ollama' | 'openai-compat' | 'stencil-server';

export interface LlmSettings {
  provider: LlmProvider;
  /** ollama / openai-compat endpoint. */
  baseUrl: string;
  model: string;
  /** openai-compat bearer key; never sent to any other provider. */
  apiKey: string;
  /** stencil-server endpoint; its token comes from the saved connection, not from here. */
  serverUrl: string;
  /** §12 per-project chat persistence — OFF by default. */
  saveChats: boolean;
}

export declare const PROVIDERS: LlmProvider[];
/** Provider → pre-filled default base URL ('' for stencil-server). */
export declare const PROVIDER_BASE_URLS: Record<string, string>;
export declare const URL_KEYS: string[];
export declare const isHttpUrl: (v: unknown) => boolean;
/** THE provider-switch rule: swap the provider, pre-filling its default base URL unless overridden. */
export declare const withProvider: (settings: LlmSettings, provider: LlmProvider) => LlmSettings;
export declare const defaultSettings: () => LlmSettings;
/** Saved overrides merged over the defaults; bad or missing data degrades to defaults. */
export declare const loadLlmSettings: () => LlmSettings;
/** The LIVE connection's bearer token for `url`, else the saved one, else ''. */
export declare const serverBearerToken: (app: DrawingApp | null | undefined, url: string) => string;
export declare const saveLlmSettings: (s: Partial<LlmSettings>) => void;
