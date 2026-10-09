export declare const resetTarget: (target: EventTarget | null) => HTMLSelectElement | HTMLInputElement | null;
/** What a logo dropped on `target` resets: a double-click's target, or a number / formula field with a stated default. */
export declare const dropResetTarget: (target: EventTarget | null) => HTMLSelectElement | HTMLInputElement | null;
export declare const defaultOf: (el: HTMLSelectElement | HTMLInputElement) => string | boolean;
export declare const resetControl: (el: HTMLSelectElement | HTMLInputElement) => boolean;
export declare const installDblReset: (root?: Document | HTMLElement) => void;
/** A colour button whose open waits out the double-click window; a second click inside it resets. */
export declare const wireColorButton: (btn: HTMLElement, hooks: { open: () => void; reset: () => void }) => void;
