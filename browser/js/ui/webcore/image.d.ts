// The webcore picture painter (config/webcore.json `image`).
/** Paint the whole picture onto a 2D context at its native size. */
export declare const paintWebcoreImage: (ctx: CanvasRenderingContext2D) => void;
/** The picture encoded as a PNG blob. */
export declare const webcoreImageBlob: () => Promise<Blob>;
