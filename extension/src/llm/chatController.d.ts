// Shapes for llm/chatController.js — the surface-independent half of the assistant turn
// (llm-contract.md §7–§11). The page injects its capabilities; the controller owns the
// history, the prompt, the plan validation and the rounds. Its listing (chatListing.js),
// open-action translation (openActions.js) and op executors (opExecutors.js) re-export
// through it, so this file is the whole module's contract.
import type { ChatMessage, LlmClient, ChatImage } from './llmClient.js';
import type { OpPlan } from './opPlan.js';

/** One entry in the working listing the model addresses by index (§8). */
export interface ListingItem {
  src?: string;
  videoUrl?: string;
  name?: string;
  alt?: string;
  w?: number;
  h?: number;
  kind?: string;
}

/** A user-dropped image, already encoded — `index` marks one matched to a listing entry. */
export interface Attachment {
  image: ChatImage;
  index?: number;
  name?: string;
}

/** What one turn produced. Typed LlmErrors propagate instead — never parsed as a plan. */
export interface TurnResult {
  reply: string;
  warnings: string[];
  /** The per-action outcomes the transcript renders. */
  cards: Array<Record<string, unknown>>;
  chatOnly: boolean;
  /** Present when the plan only gathered context and a second round ran. */
  continuation?: OpPlan;
  /** §10: the surface's confirm, resolved after every other action. */
  clearChat?: { confirmed: boolean };
}

/** Everything the page must supply; each capability throws on failure. */
export interface ChatCapabilities {
  getClient(): LlmClient;
  getListing(): ListingItem[];
  formatOfItem?(item: ListingItem): string;
  pageUrl?(): string;
  getTabs?(): Promise<unknown[]>;
  focusImage?(item: ListingItem): unknown;
  openImage?(item: ListingItem, opts?: unknown): unknown;
  attachImage?(item: ListingItem): unknown;
  pinImage?(item: ListingItem): unknown;
  unpinImage?(item: ListingItem): unknown;
  openUrlImage?(arg: { url: string }): unknown;
  scanTab?(tabId: number): unknown;
  rescan?(): unknown;
  setTheme?(mode: string): unknown;
  setFilters?(filters: unknown): unknown;
  setAccent?(key: string): unknown;
  /** Returns whether the user confirmed; only then is the history wiped. */
  clearChat?(): Promise<boolean> | boolean;
}

export interface ChatController {
  /** The replay history, trimmed to HISTORY_LIMIT and to the §7 image rule. */
  readonly history: ChatMessage[];
  /** Start a fresh conversation — history only; settings and scan state stay. */
  clearConversation(): void;
  send(text: string, opts?: { attachments?: Attachment[]; signal?: AbortSignal }): Promise<TurnResult>;
}

export declare const HISTORY_LIMIT: number;
export declare const MAX_IMAGE_EDGE: number;
export declare const MAX_ATTACHMENTS: number;
export declare const LISTING_LIMIT: number;
export declare const LISTING_NAME_CHARS: number;
export declare const LISTING_ALT_CHARS: number;
export declare const TABS_LIMIT: number;
export declare const TAB_TITLE_CHARS: number;
export declare function createChatController(capabilities: ChatCapabilities): ChatController;
