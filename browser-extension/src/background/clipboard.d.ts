// The service worker's clipboard write, through one offscreen document.

/** True once `text` is on the clipboard; false when the offscreen copy failed. */
export declare const copyText: (text: string) => Promise<boolean>;
