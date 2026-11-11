// How a Copy or a Share entry hands something on: the clipboard, or the Web Share API.

/** Text (or a promise of it) onto the clipboard, the write started inside the click. */
export declare const copyText: (text: string | Promise<string>, label?: string) => Promise<void>;
/** A picture (or a promise of one) onto the clipboard as PNG. */
export declare const copyImage: (blob: Blob | Promise<Blob>, label?: string) => Promise<void>;
/** A link through the share sheet; copied where the browser has none. */
export declare const shareLink: (url: string, title?: string) => Promise<void>;
/** A picture through the share sheet as a file. */
export declare const shareImage: (blob: Blob | Promise<Blob>, name?: string, title?: string) => Promise<void>;
