/** Evaluate what the user typed into a numeric field ("45 + 9", "* 9" on a current value). */
export declare const evalNumericExpression: (text: string, current?: number) => number | null;

/** Idle debounce before a typed expression commits itself. */
export declare const COMMIT_DEBOUNCE_MS: number;

/** Upgrade one numeric input in place (type="number" → arithmetic-capable text). Idempotent. */
export declare const enhanceNumericInput: (el: HTMLInputElement) => void;
/** Enhance every numeric input under `root`. */
export declare const enhanceNumericInputs: (root?: ParentNode) => void;
/** Install a one-time observer so inputs rendered later are upgraded as they appear. */
export declare const watchNumericInputs: (root?: Element) => MutationObserver;
