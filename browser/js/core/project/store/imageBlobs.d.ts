// Project images as IndexedDB keeps them: a Blob and the signature of one write, read back as an
// object URL, or as the data URL itself for the one key this tab holds.

/** What IndexedDB stores under a stencil_image_<id> key. */
export interface ImageRecord { blob: Blob; sig: string; }

export declare const isImageRecord: (v: unknown) => v is ImageRecord;
/** A Blob's bytes as a base64 data URL. */
export declare const dataUrlOfBlob: (blob: Blob) => Promise<string>;

export interface ImageMirror {
  has(key: string): boolean;
  keys(): IterableIterator<string>;
  /** Hold what IndexedDB returned: a record, or an older build's data-URL string. */
  adopt(key: string, value: ImageRecord | string): void;
  /** The held data URL, else the key's object URL (made on first read); undefined when empty. */
  read(key: string): string | undefined;
  /** What to persist for `value` (null once superseded), or null when the key already holds it. */
  put(key: string, value: string): Promise<ImageRecord | string | null> | null;
  /** The key's image as a data URL; `keep` (default) makes it the one held. */
  materialize(key: string, keep?: boolean): Promise<string | null>;
  drop(key: string): void;
}

export declare const createImageMirror: (urls?: Pick<typeof URL, 'createObjectURL' | 'revokeObjectURL'>) => ImageMirror;
