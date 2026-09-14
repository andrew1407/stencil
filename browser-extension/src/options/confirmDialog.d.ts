// Shapes for options/confirmDialog.js — stands in for window.confirm() (the options
// page has no native modal). `anchor` is the button the particles fly out of and back into.

/** Resolves true on Yes/Enter, false on No/Esc/backdrop-click. */
export declare const confirmDialog: (message: string, anchor: Element) => Promise<boolean>;
