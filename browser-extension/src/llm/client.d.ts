// Shapes for llm/client.js — the §6 wire mappings. The module itself is a byte-pinned
// port of browser/js/llm/client.js (tests/portParity.test.js), so these shapes are the
// browser's too.
import type { LlmProvider, LlmSettings } from './settings.js';

/** One image attachment, already base64 (no data: prefix). */
export interface ChatImage {
  mediaType: string;
  data: string;
}

export interface ChatMessage {
  role: 'user' | 'assistant';
  text: string;
  images?: ChatImage[];
}

/** Why a turn failed. The kind decides what the UI offers next. */
export type LlmErrorKind =
  | 'config' | 'network' | 'http' | 'badReply' | 'truncated' | 'refusal' | 'disabled';

export declare class LlmError extends Error {
  constructor(message: string, kind: LlmErrorKind, extra?: Record<string, unknown>);
  kind: LlmErrorKind;
  /** The endpoint replied (so the message is ITS words, quoted once), and with what status. */
  answered?: boolean;
  status?: number;
  static config(message: string): LlmError;
  static network(message: string): LlmError;
  static http(message: string): LlmError;
  static badReply(message: string): LlmError;
  static truncated(message: string): LlmError;
  static refusal(message: string): LlmError;
  static disabled(message: string): LlmError;
}

export interface LlmClient {
  chat(turn: { system: string; messages: ChatMessage[]; signal?: AbortSignal }): Promise<string>;
}

export interface ProbeResult {
  ok: boolean;
  provider?: LlmProvider;
  url?: string;
  model?: string;
  error?: string;
}

export declare const PROVIDER_LABELS: Record<string, string>;
/** Bounded, control-free, and never an API key or a URL — untrusted provider prose. */
export declare function sanitizeProviderText(text: unknown): string;
export declare function createLlmClient(opts: {
  settings: Partial<LlmSettings>;
  fetchImpl?: typeof fetch;
  getToken?: (serverUrl: string) => string | Promise<string>;
}): LlmClient;
export declare function fetchLlmInfo(
  serverUrl: string,
  opts?: { token?: string; fetchImpl?: typeof fetch },
): Promise<{ enabled: boolean; model: string }>;
export declare function listModels(settings: Partial<LlmSettings>, opts?: Record<string, unknown>): Promise<string[]>;
export declare function probeProvider(settings: Partial<LlmSettings>, opts?: Record<string, unknown>): Promise<ProbeResult>;
