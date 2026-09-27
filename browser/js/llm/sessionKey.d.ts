// Shapes for llm/sessionKey.js — the anthropic session key (llm-providers.md §5): the user's own
// key in this tab's sessionStorage, dropped when the tab closes, the TTL passes or it is forgotten.

/** A held key and when it lapses (epoch ms). */
export interface HeldSessionKey { key: string; expiresAt: number; }

/** The sessionStorage item: JSON of a HeldSessionKey. */
export declare const SESSION_KEY_ITEM: string;
/** providers.json anthropic.sessionKey.ttlMinutes, in ms. */
export declare const SESSION_KEY_TTL_MS: number;
export declare const forgetSessionKey: () => void;
/** The held key, or null; an expired or malformed record is dropped on read. */
export declare const readSessionKey: (now?: number) => HeldSessionKey | null;
/** The held key, or '' when none. */
export declare const sessionKey: (now?: number) => string;
/** Holds the key for the TTL and returns its expiry; an empty key forgets; 0 = not held. */
export declare const setSessionKey: (key: unknown, now?: number) => number;
