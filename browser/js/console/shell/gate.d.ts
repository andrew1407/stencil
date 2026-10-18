// CLI mode as the facade sees it: the switch, and the refusal every UI member throws through.
export declare const cliModeOn: () => boolean;
/** Returns the state; publishes EVENTS.cliModeChanged on a change. */
export declare const setCliMode: (on: boolean) => boolean;
/** Throws `stencil: CLI mode is on — <what> waits until /clioff` while it is on. */
export declare const cliModeGate: (what: string) => void;
export declare const GATED_SETTINGS: readonly string[];
