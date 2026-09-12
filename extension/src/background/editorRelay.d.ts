// Shapes for background/editorRelay.js — the wrappers every editor-mode handler is
// built from: answer-exactly-once, the privileged-sender gate, and the timeboxed
// ask-one-editor-tab leg. `msg`/`sender` stay opaque (chrome's own message/sender shapes).

/** A request/response handler; returning true keeps the message port open. */
export type RelayHandler = (msg: Record<string, unknown>, sender: unknown) => unknown;
export type MessageListener = (msg: Record<string, unknown>, sender: unknown, sendResponse: (res: unknown) => void) => true;

/** Never rejects: a throw inside `fn` becomes `{ ok:false, error }`. */
export declare const answers: (fn: RelayHandler) => MessageListener;
/** chrome.tabs.get, but null instead of a throw for a tab that has closed. */
export declare const getTab: (tabId: number) => Promise<unknown | null>;
/** Only our own pages or (if enabled) the editor page may call the wrapped handler. */
export declare const privileged: (fn: RelayHandler) => (msg: Record<string, unknown>, sender: unknown) => Promise<unknown>;
/** Always resolves to a reply object — a silent or missing bridge becomes `{ ok:false, error }`. */
export declare const askEditorTab: (tabId: number, message: Record<string, unknown>) => Promise<Record<string, unknown>>;
export declare const probeEditorTabs: (
  tabs: Array<{ id?: number | null }>,
  opts?: { thumbnail?: boolean },
) => Promise<Array<Record<string, unknown> | null>>;
/** An explicit `msg.tabId` wins; otherwise the sender's own tab. */
export declare const targetTabId: (msg: { tabId?: number }, sender: { tab?: { id?: number } }) => number | undefined;
export declare const editorTabFor: (
  msg: Record<string, unknown>,
  sender: unknown,
) => Promise<{ tab: unknown; error?: undefined } | { tab?: undefined; error: string }>;
