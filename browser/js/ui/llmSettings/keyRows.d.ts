// Shapes for ui/llmSettings/keyRows.js — the anthropic session-key rows of the assistant settings:
// the password field, the held key's expiry line and Forget (llm-providers.md §5).
import type { HeldSessionKey } from '../../llm/sessionKey.js';

export declare const SESSION_STORAGE_BLOCKED_TEXT: string;
/** "Key kept for this tab until 21:40." (weekday added on another day), or "No key for this session." */
export declare const sessionKeyStatusText: (held: HeldSessionKey | null, now?: number) => string;

export interface SessionKeyRows {
  /** Shows the rows (anthropic) or hides them; either way the field starts empty. */
  show(on: boolean): void;
  /** The typed key, else the held one, else ''. */
  requestKey(): string;
  /** Holds a typed key for the TTL; false when the browser refused session storage. */
  commit(): boolean;
}
export declare const wireSessionKeyRows: (
  host: ParentNode | null | undefined,
  hooks?: { onInput?: () => void; onChange?: () => void; onForget?: () => void },
) => SessionKeyRows;
