// Shape of webCommands.js — the hand-offs to the browser and the desktop, and the console.
export declare const HANDLERS: Record<string, () => Promise<unknown>>;
export declare const OPEN_A_FILE: string;
export declare const OPEN_A_SCRIPT: string;
export declare const OUTPUT_NAME: string;
export declare const STCJS_IS_CONSOLE_ONLY: string;
export declare const TOO_BIG: string;
export declare const openImageInWeb: () => Promise<unknown>;
export declare const openInWeb: () => Promise<unknown>;
export declare const openInWebIncognito: () => Promise<unknown>;
/** The script in the web app's Script window, not run. */
export declare const openScriptInWeb: () => Promise<unknown>;
/** The desktop app, over a `stencil://` link: the script opens in its Script window; Run is pressed there. */
export declare const openInDesktop: () => Promise<unknown>;
export declare const runInDesktop: () => Promise<unknown>;
export declare const runInDesktopIncognito: () => Promise<unknown>;
export declare const runInWebConsole: () => Promise<unknown>;
export declare const runSelectionInWebConsole: () => Promise<unknown>;
export declare const runExpression: (expression: string, opts?: { timeoutMs?: number }) => Promise<unknown>;
export declare const register: (context: unknown) => Record<string, () => Promise<unknown>>;
