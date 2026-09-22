// Pull an image URL out of a cross-page drag's payloads (uri-list / html / plain), fetch it
// into a File so the drag flows through the dropped-file path, and read media Files off a
// DataTransfer synchronously. DOM-free.

export declare const looksLikeImageUrl: (u: unknown) => boolean;
/** The drag's candidate URLs, best first: image-looking ones, then source order; a known host's
    CORS-reachable twin follows the url it was derived from. */
export declare const extractDraggedImageUrls: (read: (mimeType: string) => string | null | undefined) => string[];
/** The first candidate, or ''. `read(type)` may return '' or throw. */
export declare const extractDraggedImageUrl: (read: (mimeType: string) => string | null | undefined) => string;
/** Fetch a dragged URL into a File; rejects with the message the callers surface. */
export declare const fetchDraggedMediaFile: (url: string, opts?: { accept?: RegExp }) => Promise<File>;
/** The first candidate (twins included) that answers with an accepted type; rejects naming the
    count tried and the first failure's host. */
export declare const fetchFirstDraggedMediaFile: (urls: string | string[], opts?: { accept?: RegExp }) => Promise<File>;
/** A readable filename for a fetched drag; data:/blob: URLs get `image.<ext>`. */
export declare const fileNameForUrl: (url: unknown, mime?: string) => string;
export declare const mediaFilesFromData: (dt: DataTransfer | null | undefined) => File[];
