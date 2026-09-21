// Shape of versionCache.js — a small (document, version) memo the buffer-readers share.
export declare const LIMIT: number;
export declare const keyFor: (document: unknown) => string | null;
export declare const versionCache: <T>(opts?: { limit?: number }) => {
  get: (document: unknown, compute: (document: any) => T) => T;
  forget: (uri: unknown) => void;
};
