// Shapes for llm/surface.js — the ONE module the byte-pinned client.js is allowed
// to differ through: this surface's wording and its stencil-server token resolution.
import type { LlmSettings } from './settings.js';

export declare const ASSISTANT_OFF_TEXT: string;
/** The surface's one loopback classifier (lib/connection), where a key may ride plain http. */
export declare function isLoopbackHost(host: string): boolean;
/** The stored connection's token for that server, else the explicit settings token. */
export declare function serverTokenFor(
  serverUrl: string,
  ctx?: { connections?: Array<{ url: string; token?: string }>; settings?: Partial<LlmSettings> },
): string;
export declare function defaultGetToken(settings: Partial<LlmSettings>): (serverUrl: string) => Promise<string>;
/** What a failed turn shows — the reason said ONCE (contract §6.3). */
export declare function turnFailureText(settings: Partial<LlmSettings> | null, err: unknown): string;
/** A turn refused for want of an anthropic session key. */
export declare function isMissingSessionKey(err: unknown, settings?: Partial<LlmSettings> | null): boolean;
/** The provider itself is unreachable or unconfigured (or has no session key), so the options are the fix. */
export declare function isUnreachableError(err: unknown, settings?: Partial<LlmSettings> | null): boolean;
