// "Copy Stencil link": a link opening the Stencil app on the clicked element's source, copied.

/** The notes the page shows after a copy. */
export declare const NOTES: Readonly<{ copied: string; copiedIncognito: string; unshareable: string; failed: string }>;
/** Copies the link for the clicked item; the link on success, null when none could be made or copied. */
export declare const copyLinkFromMenu: (info: { menuItemId: string | number; srcUrl?: string; frameId?: number }, tab?: unknown, tabId?: number) => Promise<string | null>;
