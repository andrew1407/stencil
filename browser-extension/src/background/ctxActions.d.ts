// Shapes for background/actions.js — the per-click-group async handler that
// chrome.contextMenus.onClicked dispatches to. info/tab stay opaque (chrome's own
// OnClickData/Tab); the handlers only read a few fields, never the whole shape.

/** A resolved click handler: `info`/`tab` are chrome's own types, kept opaque here. */
export type ClickHandler = (info: unknown, tab?: unknown, tabId?: number) => Promise<void>;

/** Picks the handler for this click by menu item id — never itself async. */
export declare const resolveClickHandler: (info: { menuItemId: string | number }) => ClickHandler;
