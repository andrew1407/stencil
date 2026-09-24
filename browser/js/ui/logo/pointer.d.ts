/** Start remembering the pointer's last page position (idempotent). */
export declare const trackPointer: (doc?: Document) => void;
/** The pointer's last client position over the page, or null until it has been seen. */
export declare const lastPointer: () => { x: number; y: number } | null;
