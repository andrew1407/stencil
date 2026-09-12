// Shared video-frame capture: decode a video ONCE and grab still frames as JPEG data URLs
// (≤ 1920px on the long edge). Used by stencil.load(videoUrl), the open-image modal and the
// chat's frame attachments. Browser-only.

/** By MIME, falling back to the extensions in config/mediaTypes.json. Pure. */
export declare function isVideoFile(file: { type?: string; name?: string } | null | undefined): boolean;
/** By path extension, tolerating a ?query / #hash. Pure. */
export declare function isVideoUrl(url: unknown): boolean;
/** Revokes `srcUrl` when done; rejects on load/seek/taint/timeout. */
export declare function videoFrameDataUrl(srcUrl: string, timeSec: number): Promise<string>;
/** A JPEG File reusing the video's base name; `timeSec` selects the frame. */
export declare function videoFileToImageFile(file: File, timeSec?: number): Promise<File>;
/** `count` evenly-spaced frames (first frame included). */
export declare function videoFrameSamples(file: File, count?: number): Promise<string[]>;
/** Frame INDEX → seek time at a nominal 30 fps (the CLI's exact pick approximated). */
export declare function videoFrameByIndex(file: File, index: number): Promise<string>;
