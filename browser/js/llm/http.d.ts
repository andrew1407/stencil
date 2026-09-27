// Shapes for llm/http.js — the typed error every chat failure arrives as, the provider-prose
// sanitizer, the server's upstream classifier and the POST and GET the wire mappings share. The module is byte-pinned with
// browser-extension/src/llm/http.js (browser-extension/tests/portParity.test.js); this file is not.

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
/** 8 MiB: the most a provider's reply buffers, as the CLI and mcp cap it. */
export declare const MAX_REPLY_BYTES: number;
/** A provider's reply as JSON, refused past MAX_REPLY_BYTES. */
export declare const readReply: (resp: Response | { json(): Promise<unknown> }) => Promise<unknown>;

/** server/internal/llm/upstream.go's conditions; '' = unrecognised. */
export type UpstreamKind = 'credits' | 'auth' | 'model' | 'rateLimit' | 'timeout' | 'overloaded' | '';
export declare const classifyUpstream: (status: number, errType?: string, message?: string) => UpstreamKind;
/** Does the text show any 8-character run of the secret? */
export declare const containsSecretFragment: (text: string, secret: string) => boolean;
/** The recognised reason, else "…returned an error (HTTP n)" plus the sanitized, key-free upstream text. */
export declare const upstreamErrorText: (status: number, errType: string, message: string, secret: string) => string;

/** What a non-2xx answer says: the message, and whether it is the typed 'disabled' kind. */
export type DescribeFailure = (status: number, body: unknown) => { message: string; disabled?: boolean };
/** The Anthropic error envelope, classified as the server classifies its upstream. */
export declare const upstreamFailure: (secret: string) => DescribeFailure;
/** The keyed wire's transport rule: throws the typed 'disabled' error for plain http off loopback. */
export declare const keyedInit: (url: string, isLoopbackHost: (host: string) => boolean) => RequestInit;
/** Every LLM request's redirect mode: a 30x fails the request rather than carry a key onward. */
export declare const NO_REDIRECT: RequestRedirect;
/** POST JSON and return the parsed body; every non-2xx becomes a typed LlmError. */
export declare const postJson: (
  fetchImpl: typeof fetch | undefined, url: string, body: unknown,
  headers?: Record<string, string>, signal?: AbortSignal, describe?: DescribeFailure, init?: RequestInit,
) => Promise<unknown>;
/** GET under a timeout; a non-JSON 2xx body reads as {}, a non-2xx body as null. */
export declare const getInfo: (
  fetchImpl: typeof fetch, url: string, headers: Record<string, string>, timeoutMs: number, init?: RequestInit,
) => Promise<{ ok: boolean; status: number; body: unknown }>;
