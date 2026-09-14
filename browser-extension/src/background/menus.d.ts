// Shapes for background/menus.js — the context menu, rebuilt from scratch on every
// worker start.

export declare const buildMenus: () => void;
/** Whether a desktop URL scheme is configured; cached for the synchronous CTX handler. */
export declare const desktopSchemeSet: boolean;
/** Re-reads the setting and shows/hides the desktop-app menu items to match. */
export declare const syncDesktopMenuVisibility: () => Promise<void>;
