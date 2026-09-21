// Per-project chat persistence (llm-contract.md §12): the stored document + the IndexedDB
// store. OPT-IN, text-only; images are never persisted, §7's continuation note and raw
// op-plans are refused on BOTH sides of the store. Every operation is best-effort.

export interface ChatDocMessage { role: 'user' | 'assistant'; text: string; }

/** The §12.1 document. An unknown version reads as "no saved chat", never as an error. */
export interface ChatDoc {
  version: number;
  savedAt: number;
  messages: ChatDocMessage[];
}

/** A minimal async key/value store over one object store; keys are project ids. */
export interface ChatStoreBackend {
  get(key: string): Promise<unknown>;
  set(key: string, value: ChatDoc): Promise<unknown>;
  remove(key: string): Promise<unknown>;
  clear(): Promise<unknown>;
}

export interface ChatStore {
  /** null for no saved chat, a missing backend, or corrupt bytes. */
  load(projectId: string | null | undefined): Promise<ChatDoc | null>;
  save(projectId: string | null | undefined, doc: ChatDoc | null | undefined): Promise<boolean>;
  remove(projectId: string | null | undefined): Promise<void>;
  clear(): Promise<void>;
}

/** A visible transcript row as the store reads it (ui/chatView.js renders the rest). */
export interface ChatRowLike {
  role?: string;
  text?: string;
  error?: boolean;
  card?: boolean;
}

export declare const CHAT_DOC_VERSION: number;
/** §7's history bound — the persisted transcript never exceeds the replay window. */
export declare const CHAT_MESSAGE_LIMIT: number;
/** §7's continuation note (config/llm/systemPrompt.json) — never stored, never displayed. */
export declare const CONTINUATION_NOTE: string;
/** The continuation note (or any bracketed variant), or a raw op-plan as the ASSISTANT turn. */
export declare const isInternalChatText: (role: string | undefined, text: unknown) => boolean;
/** The §12.1 gate over stored messages, applied wherever a document is about to be believed. */
export declare const sanitizeChatMessages: (messages: ReadonlyArray<ChatRowLike> | null | undefined) => ChatDocMessage[];
export declare const buildChatDoc: (messages: ReadonlyArray<ChatRowLike> | null | undefined, now?: number) => ChatDoc;
/** A stored document (object or JSON string) → the validated doc, or null. */
export declare const parseChatDoc: (raw: unknown) => ChatDoc | null;
/** Transcript rows → persistable turns: settled user/assistant text only (no pending, no cards). */
export declare const rowsToMessages: (rows: ReadonlyArray<ChatRowLike> | null | undefined) => ChatDocMessage[];
/** null when IndexedDB is missing (Node), so createChatStore degrades to no-persistence. */
export declare const createIdbBackend: (idb?: IDBFactory | null) => ChatStoreBackend | null;
export declare const createChatStore: (backend?: ChatStoreBackend | null) => ChatStore;
