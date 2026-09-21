/** Told the new text whenever another view changes the shared script. */
export type ScriptView = (text: string) => void;

/** The script both .stc editors are showing. Empty until something is typed or loaded. */
export declare const scriptText: () => string;

/**
 * Replaces the shared script and tells every view but `from` — the one already showing it.
 * In memory only: nothing here is persisted, so a reload starts empty.
 */
export declare const setScriptText: (next: string, from?: ScriptView | null) => void;

/** Registers a view and returns the call that drops it again. */
export declare const subscribeScript: (view: ScriptView) => () => void;
