/** Class marking a `.ctrl-section` that begins a wrapped row, dropping the hairline before it. */
export declare const WRAPPED_SEP_CLASS: string;

/** Mark each `.ctrl-section` under `root` that begins a wrapped row; one pass, no layout feedback. */
export declare function syncWrappedSeparators(root: Element | null): void;
