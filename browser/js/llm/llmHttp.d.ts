// Shapes for llm/llmHttp.js — the typed error every chat failure arrives as, the provider-prose
// sanitizer and the one JSON POST the wire mappings share. The module is byte-pinned with
// browser-extension/src/llm/llmHttp.js (browser-extension/tests/portParity.test.js); this file is not.

/** Why a turn failed; the kind decides what the UI offers next. */
export type LlmErrorKind = 'config' | 'network' | 'http' | 'badReply' | 'truncated' | 'refusal' | 'disabled';

export declare class LlmError extends Error {
  constructor(message: string, kind: LlmErrorKind);
  name: 'LlmError';
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

/** Bounded, control-free, URLs and token-shaped runs redacted — untrusted provider prose. */
export declare const sanitizeProviderText: (text: unknown) => string;
/** POST JSON and return the parsed body; every non-2xx becomes a typed LlmError. */
export declare const postJson: (
  fetchImpl: typeof fetch | undefined, url: string, body: unknown,
  headers?: Record<string, string>, signal?: AbortSignal,
) => Promise<unknown>;
