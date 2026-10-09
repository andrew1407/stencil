// Shape of launch.js — the `#stencil=` hand-off the browser app boots on.
export interface LaunchPayload {
  script?: string; scriptMode?: 'open' | 'run'; dataUrl?: string; src?: string; name?: string;
  incognito?: boolean;
  layout?: Record<string, unknown>;
}
export declare const IMAGE_TYPES: Readonly<Record<string, string>>;
export declare const MAX_PAYLOAD: number;
/** Bytes of a local image whose data URL still fits MAX_PAYLOAD. */
export declare const MAX_INLINE_BYTES: number;
/** statSync only: false past MAX_INLINE_BYTES, true for an unreadable path. */
export declare const fitsInline: (path: string) => boolean;
export declare const buildLaunchUrl: (base: string, payload: LaunchPayload) => string;
/** Null for an unknown type, an unreadable file or one past MAX_INLINE_BYTES (never read). */
export declare const imageDataUrl: (path: string) => { dataUrl: string; name: string } | null;
export declare const imagePart: (image: string, opts?: { inline?: boolean }) => LaunchPayload | null;
export declare const isRemote: (spec: unknown) => boolean;
export declare const localSources: (blocks: readonly { source: string; kind: string }[]) => string[];
export declare const projectLaunch: (text: string) => LaunchPayload | null;
export declare const scriptLaunch: (script: string, image?: string, opts?: { inline?: boolean }) => LaunchPayload;
export declare const isTooBig: (url: unknown) => boolean;
