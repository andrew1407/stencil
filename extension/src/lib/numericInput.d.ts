export declare const COMMIT_DEBOUNCE_MS: number;

/** Evaluate a numeric field's raw text, resolving a leading `*`/`/`/`**` against `current`. */
export declare function evalNumericExpression(text: string, current?: number): number | null;
/** Upgrade one `<input type="number">` in place; idempotent. */
export declare function enhanceNumericInput(el: HTMLInputElement): void;
export declare function enhanceNumericInputs(root?: ParentNode): void;
export declare function watchNumericInputs(root?: Node): MutationObserver;
