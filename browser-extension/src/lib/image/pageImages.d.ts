export interface ManifestIcon { src: string; }
export interface WebManifest { icons?: ManifestIcon[]; }

export declare function bgImageUrl(cssValue: string): string;
export declare function cssImageUrls(cssValue: string): string[];
export declare function srcsetUrls(srcset: string): string[];
export declare function manifestIconUrls(manifest: WebManifest | null | undefined, manifestUrl: string): string[];
export declare function nameFromUrl(url: string, fallback?: string): string;
export declare function videoHasFrame(v: HTMLVideoElement | null | undefined): boolean;
