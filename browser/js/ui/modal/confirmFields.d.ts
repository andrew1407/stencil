/** Wraps `el` in a .confirm-choose-row at the end of the dialog body. */
export declare const injectRow: (body: HTMLElement, el: HTMLElement) => void;
/** askAlt's third button (#confirm-modal-alt): `altIcon` + `altLabel`, `onClick` on press. */
export declare const altButton: (opts: { altIcon?: string; altLabel?: string }, onClick: () => void) => HTMLButtonElement;
/** choose's <select> over `{value, label}` options; a missing label shows the value. */
export declare const choiceSelect: (options?: { value: string; label?: string }[]) => HTMLSelectElement;
/** prompt's field: a <textarea> of `rows` (default 3) when `multiline`, else a text input. */
export declare const promptField: (opts: { multiline?: boolean; rows?: number; defaultValue?: string }) => HTMLInputElement | HTMLTextAreaElement;
/** The hidden line under the field that says why the value cannot be accepted. */
export declare const promptReason: () => HTMLDivElement;
