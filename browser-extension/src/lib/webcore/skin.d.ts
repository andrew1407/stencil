export declare const isSkinOn: (doc?: Document) => boolean;
/** True where the browser's hold would open its webcore row: grey, and the user's own None. */
export declare const holdAllowed: (win?: Window & typeof globalThis) => boolean;
export declare const installWebcore: (doc?: Document, win?: Window & typeof globalThis) => void;
export declare const wireWebcoreHold: (wrap: Element | null | undefined,
  opts?: { holdMs?: number; win?: Window & typeof globalThis }) => void;
