// Shapes for llm/sessionKey.js — the anthropic session key (llm-providers.md §5) in
// chrome.storage.session: dropped when the browser closes, the extension reloads, the TTL passes
// or it is forgotten; trusted contexts only.

/** A held key and when it lapses (epoch ms). */
export interface HeldSessionKey { key: string; expiresAt: number; }

/** The chrome.storage.session key holding a HeldSessionKey. */
export declare const SESSION_KEY_ITEM: string;
/** providers.json anthropic.sessionKey.ttlMinutes, in ms. */
export declare const SESSION_KEY_TTL_MS: number;
/** Pins chrome.storage.session to trusted contexts (the worker calls it on start). */
export declare const lockSessionKeyArea: () => Promise<void>;
export declare const forgetSessionKey: () => Promise<void>;
/** The held key, or null; an expired or malformed record is dropped on read. */
export declare const readSessionKey: (now?: number) => Promise<HeldSessionKey | null>;
/** The held key, or '' when none. */
export declare const sessionKey: (now?: number) => Promise<string>;
/** Holds the key for the TTL and resolves its expiry; an empty key forgets; 0 = not held. */
export declare const setSessionKey: (key: unknown, now?: number) => Promise<number>;
