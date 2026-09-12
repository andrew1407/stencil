// The bytes half of the Open-Image dialog: fetch a URL into a File, and turn a video into
// a captured still frame. No DOM — the dialog passes the chosen frame index in.

/** Same-origin / data: / CORS-enabled only; rejects with `HTTP <status>` on a failed fetch. */
export declare const fetchUrlToFile: (url: string) => Promise<File>;
/** A video File becomes its captured frame; an image passes through untouched. */
export declare const toFrameIfVideo: (file: File, frameIndex?: number) => Promise<File>;
