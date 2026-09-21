// The app's ONE assistant conversation (llm-contract.md §7): the chat panel and the
// context-menu chat are two views of the SAME conversation — one memoized controller per
// app, one visible transcript (rows are data; ui/chatView.js owns the DOM), one turn in
// flight at a time. Nothing here touches the DOM.
import type { DrawingApp } from '../../core/drawingApp.js';
import type { LlmSettings } from '../llmSettings.js';
import type { ProbeResult } from '../llmClient.js';
import type { AskPreview, PlanAsk, VariantResult } from '../plan/opPlan.js';
import type { ChatController, ChatControllerOptions, TurnResult } from './chatController.js';
import type { ChatErrorDescription } from './chatReply.js';
export { uniqueProjectName, resolveProjectByName } from '../projectNames.js';
export { replyWithWarnings, EMPTY_REPLY_TEXT, settledReplyText, unreachableText, describeChatError } from './chatReply.js';

/** What a transcript can show of a queued attachment (a video: its first frame). */
export interface AttachmentPreview { name: string; kind: 'image' | 'video'; dataUrl: string; }

/** One row of the shared transcript. `text` is model output — rendered with textContent only. */
export interface ChatRow {
  id: number;
  role: 'user' | 'assistant';
  text: string;
  /** The in-flight "…" assistant row. */
  pending?: boolean;
  error?: boolean;
  /** What a failed turn tried, for Retry / Resend. */
  retryText?: string;
  /** Render as the unreachable / expired card rather than a bubble. */
  card?: boolean;
  /** The server whose session expired — the card's Reconnect target — or null. */
  reconnect?: string | null;
  attachments?: AttachmentPreview[];
  results?: VariantResult[];
  ask?: PlanAsk | null;
  askPreviews?: AskPreview[];
}

/** runChatTurn's outcome — never thrown, so both surfaces render the same thing. */
export type ChatTurnResult =
  | { ok: true; text: string; entry: TurnResult }
  | ({ ok: false; error: unknown } & ChatErrorDescription);

export interface LoggedTurnHooks {
  settings?: LlmSettings;
  /** Receives the turn's AbortController (null without one) so a surface can offer Stop. */
  begin?: (abort: AbortController | null) => void;
  onResult?: (res: ChatTurnResult) => void;
  cleanup?: () => void;
}

/** A notification balloon for a turn that landed while the surface was closed. */
export interface TurnToast { text: string; type: 'ok' | 'fail'; }

/** Created on FIRST USE (so it captures the frozen facade); `create` is a test seam. */
export declare const sharedChatController: (app: DrawingApp, opts?: { create?: (opts: ChatControllerOptions) => ChatController }) => ChatController;
/** The controller if one exists — never creates it. */
export declare const peekChatController: (app: DrawingApp) => ChatController | null;
export declare const forgetChatController: (app: DrawingApp) => boolean;

export declare const chatLog: () => ChatRow[];
/** Subscribe to every transcript change; returns the unsubscribe. */
export declare const onChatLog: (fn: (rows: ChatRow[]) => void) => () => void;
export declare const appendChatRow: (row: Omit<ChatRow, 'id'>) => ChatRow;
export declare const updateChatRow: (id: number, patch: Partial<ChatRow>) => ChatRow | undefined;
export declare const clearChatLog: () => void;
/** THE clear-conversation path: controller state, the transcript, and the composer chips. */
export declare const clearSharedConversation: (app: DrawingApp) => void;
/** Test seam: drops the rows AND the subscribers. */
export declare const resetChatLog: () => void;
export declare const chatTurnInFlight: () => boolean;

export declare const ATTACHMENT_CAP_NOTICE: string;
/** Queue Files onto the controller; resolves to how many landed. `onCapped` fires once per batch. */
export declare const queueAttachments: (
  controller: ChatController, files: Iterable<File>,
  onError?: (err: unknown, file: File) => void, onCapped?: (count: number) => void,
) => Promise<number>;
export declare const attachmentPreviews: (controller: ChatController | null | undefined) => AttachmentPreview[];
/** Re-queue a logged row's previews into an EMPTY queue, as analyze-images; returns how many. */
export declare const requeueRowAttachments: (controller: ChatController | null | undefined, attachments?: ReadonlyArray<Partial<AttachmentPreview>>) => number;

export declare const PROBE_TTL_MS: number;
export declare const cacheProbe: (settings: Partial<LlmSettings>, probe: ProbeResult) => void;
/** The cached probe for these settings while younger than PROBE_TTL_MS, else null. */
export declare const cachedProbe: (settings: Partial<LlmSettings>, now?: number) => ProbeResult | null;
export declare const forgetProbe: () => void;
/** The .conn-status class suffix: 'connecting' (no probe yet), 'connected', or 'error'. */
export declare const probeStatusClass: (probe: ProbeResult | null | undefined) => 'connecting' | 'connected' | 'error';

export declare const runChatTurn: (controller: ChatController, text: string, opts?: { signal?: AbortSignal; settings?: LlmSettings }) => Promise<ChatTurnResult>;

export declare const CHAT_TOAST_CHARS: number;
export declare const SPOKEN_ECHO_CHARS: number;
export declare const truncateForToast: (text: string, max?: number) => string;
/** A spoken prompt's opening words, on one line, for the "Sent" balloon. */
export declare const spokenEcho: (text: unknown) => string;
/** The balloon a landed turn deserves, or null when nothing should be said (an abort). */
export declare const closedTurnToast: (res: ChatTurnResult | null | undefined) => TurnToast | null;
/** One LOGGED turn: user row + pending row, Stop wiring, the outcome patched in; never throws. */
export declare const runLoggedChatTurn: (controller: ChatController, text: string, hooks?: LoggedTurnHooks) => Promise<ChatTurnResult>;
