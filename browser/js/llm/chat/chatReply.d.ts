// Shapes for llm/chatReply.js — what the user reads when a turn lands or fails (§6.3): the
// reply plus its warnings, and the table that maps a failed turn to a kind, a text and a card.
// Pure; the DOM decisions live in the views.
import type { LlmSettings } from '../llmSettings.js';
import type { TurnResult } from './chatController.js';

export type ChatErrorKind = 'abort' | 'refusal' | 'notice' | 'expired' | 'unreachable' | 'error';

export interface ChatErrorDescription {
  kind: ChatErrorKind;
  text: string;
  /** Present for 'expired': the collaboration server that refused the token. */
  serverUrl?: string;
}

/** The reply plus any warnings appended in parentheses (contract §1). */
export declare const replyWithWarnings: (entry: Pick<TurnResult, 'reply' | 'warnings'> | null | undefined) => string;
export declare const EMPTY_REPLY_TEXT: string;
export declare const settledReplyText: (entry: Pick<TurnResult, 'reply' | 'warnings'> | null | undefined) => string;
/** "Couldn't reach <provider> at <host> (<why>)", or the endpoint's own words when it answered. */
export declare const unreachableText: (settings: Partial<LlmSettings> | null | undefined, err: unknown) => string;
/** Map a failed turn to what the user sees; pure — the DOM decisions live in the views. */
export declare const describeChatError: (err: unknown, settings: Partial<LlmSettings> | null | undefined) => ChatErrorDescription;
