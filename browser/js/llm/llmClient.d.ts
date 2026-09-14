// Shapes for llm/llmClient.js — the §6 wire mappings: one chat() over the three providers,
// messages in the stencil-server DTO shape. The module is byte-pinned with
// browser-extension/src/llm/llmClient.js (browser-extension/tests/portParity.test.js); this file is not.
import type { LlmProvider, LlmSettings } from './llmSettings.js';

/** One image attachment, already base64 (no data: prefix). */
export interface ChatImage { mediaType: string; data: string; }

export interface ChatMessage {
  role: 'user' | 'assistant';
  text: string;
  images?: ChatImage[];
}

/** Why a turn failed; the kind decides what the UI offers next. */
export type LlmErrorKind = 'config' | 'network' | 'http' | 'badReply' | 'truncated' | 'refusal' | 'disabled';

export declare class LlmError extends Error {
  constructor(message: string, kind: LlmErrorKind);
  name: 'LlmError';
  kind: LlmErrorKind;
  static config(message: string): LlmError;
  static network(message: string): LlmError;
  static http(message: string): LlmError;
  static badReply(message: string): LlmError;
  static truncated(message: string): LlmError;
  static refusal(message: string): LlmError;
  static disabled(message: string): LlmError;
}

export interface LlmClient {
  /** `signal` cancels the in-flight request; aborts surface as the runtime's AbortError. */
  chat(turn: { system: string; messages: ChatMessage[]; signal?: AbortSignal }): Promise<string>;
}

/** NEVER a rejection: failures come back as { ok: false, detail }. */
export interface ProbeResult {
  ok: boolean;
  provider: LlmProvider | undefined;
  url: string;
  model: string;
  detail: string;
}

export interface ClientOptions {
  fetchImpl?: typeof fetch;
  /** Resolves the stencil-server bearer token; sync or async. */
  getToken?: (serverUrl: string) => string | Promise<string>;
  timeoutMs?: number;
}

/** 'none' is the off state, then providers.json displayNames. */
export declare const PROVIDER_LABELS: Record<string, string>;
/** Bounded, control-free, URLs and token-shaped runs redacted — untrusted provider prose. */
export declare const sanitizeProviderText: (text: unknown) => string;
export declare const createLlmClient: (opts?: { settings?: Partial<LlmSettings> } & Omit<ClientOptions, 'timeoutMs'>) => LlmClient;
/** GET {serverUrl}/llm/info; errors propagate. */
export declare const fetchLlmInfo: (serverUrl: string, opts?: { token?: string; fetchImpl?: typeof fetch }) => Promise<{ enabled: boolean; model: string }>;
/** Best-effort model suggestions; never throws, failures resolve []. */
export declare const listModels: (settings: Partial<LlmSettings> | null | undefined, opts?: ClientOptions) => Promise<string[]>;
export declare const probeProvider: (settings: Partial<LlmSettings> | null | undefined, opts?: ClientOptions) => Promise<ProbeResult>;
