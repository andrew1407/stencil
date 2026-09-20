// Shape of commands.js — the terminal commands: the CLI ones, emit, and running a .pystc.
export declare const runScript: () => Promise<unknown>;
export declare const runScriptOnImage: () => Promise<unknown>;
export declare const checkScript: () => Promise<unknown>;
export declare const emitScript: () => Promise<unknown>;
export declare const runPythonScript: () => Promise<unknown>;
export declare const HANDLERS: Record<string, () => Promise<unknown>>;
export declare const register: (context: unknown) => Record<string, () => Promise<unknown>>;
