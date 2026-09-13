export declare function isImageDataUrl(s: unknown): boolean;
export declare function fetchAsDataUrl(url: string, opts?: { pageUrl?: string }): Promise<string>;
export declare function guessMime(url: string): string;
export declare function filenameFromUrl(url: string, fallback?: string): string;
export declare function blobToDataUrl(blob: Blob): Promise<string>;
