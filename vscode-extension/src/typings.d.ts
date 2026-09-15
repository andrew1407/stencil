// Shape of typings.js — the command that puts the facade's types in the workspace.
export declare const HANDLERS: Record<string, () => Promise<unknown>>;
export declare const NO_FOLDER: string;
export declare const addTypings: () => Promise<unknown>;
export declare const register: (context: unknown) => Record<string, () => Promise<unknown>>;
export declare const targetFolder: () => string;
