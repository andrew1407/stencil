// Shape of webCommands.js — the browser commands, hand-off and console.
export declare const HANDLERS: Record<string, () => Promise<unknown>>;
export declare const OPEN_A_FILE: string;
export declare const OUTPUT_NAME: string;
export declare const STCJS_IS_CONSOLE_ONLY: string;
export declare const TOO_BIG: string;
export declare const openImageInWeb: () => Promise<unknown>;
export declare const openInWeb: () => Promise<unknown>;
export declare const openInWebIncognito: () => Promise<unknown>;
export declare const runInWebConsole: () => Promise<unknown>;
export declare const runSelectionInWebConsole: () => Promise<unknown>;
export declare const runExpression: (expression: string, opts?: { timeoutMs?: number }) => Promise<unknown>;
export declare const register: (context: unknown) => Record<string, () => Promise<unknown>>;
