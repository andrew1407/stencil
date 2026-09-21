// Ask a cross-origin host for readable pixels; `sameOrigin` skips the ask.
export declare function loadMediaCors(el: HTMLImageElement | HTMLVideoElement, src: string, sameOrigin: boolean): void;
// Re-load plainly after a refused CORS ask; false when the plain load already failed.
export declare function retryWithoutCors(el: HTMLImageElement | HTMLVideoElement, src: string): boolean;
// Whether the decoded media's pixels can be read back (crop + image-coloured dust).
export declare function canReadPixels(el: HTMLImageElement | HTMLVideoElement): boolean;
