// Shape of webLaunch.js — the `#stencil=` hand-off the browser app boots on.
export interface LaunchPayload {
  script?: string; dataUrl?: string; src?: string; name?: string;
  layout?: Record<string, unknown>;
}
export declare const IMAGE_TYPES: Readonly<Record<string, string>>;
export declare const MAX_PAYLOAD: number;
export declare const buildLaunchUrl: (base: string, payload: LaunchPayload) => string;
export declare const imageDataUrl: (path: string) => { dataUrl: string; name: string } | null;
export declare const imagePart: (image: string, opts?: { inline?: boolean }) => LaunchPayload | null;
export declare const isRemote: (spec: unknown) => boolean;
export declare const localSources: (blocks: readonly { source: string; kind: string }[]) => string[];
export declare const projectLaunch: (text: string) => LaunchPayload | null;
export declare const scriptLaunch: (script: string, image?: string, opts?: { inline?: boolean }) => LaunchPayload;
export declare const isTooBig: (url: unknown) => boolean;
