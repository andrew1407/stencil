// Thumbnails as IndexedDB keeps them: a Blob plus the signature of the data URL it came from,
// read back as an object URL made once per Blob.

/** What IndexedDB stores under a stencil_thumb_<id> key. */
export interface ThumbRecord { blob: Blob; sig: string; }

export declare const isDataUrl: (v: unknown) => v is string;
/** A data URL's bytes and media type as a Blob, synchronously. */
export declare const blobOfDataUrl: (url: string) => Blob;
export declare const isThumbRecord: (v: unknown) => v is ThumbRecord;

export interface ThumbMirror {
  has(key: string): boolean;
  keys(): IterableIterator<string>;
  /** Hold a record read back from IndexedDB, replacing (and revoking) another picture the key held. */
  adopt(key: string, record: ThumbRecord): void;
  /** The key's object URL, made on first read; undefined when the key holds nothing. */
  urlOf(key: string): string | undefined;
  /** The record to persist for `dataUrl`, or null when the key already holds that picture. */
  put(key: string, dataUrl: string): ThumbRecord | null;
  drop(key: string): void;
}

export declare const createThumbMirror: (urls?: Pick<typeof URL, 'createObjectURL' | 'revokeObjectURL'>) => ThumbMirror;
