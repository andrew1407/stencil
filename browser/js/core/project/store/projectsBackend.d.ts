// IndexedDB project storage behind ProjectsStore's synchronous localStorage-shaped contract: an
// in-memory mirror hydrated once at boot and written through asynchronously. The stencil_project_,
// stencil_image_ and stencil_thumb_ keys move to IndexedDB (an image or thumbnail as a Blob, read
// back as an object URL); the registry stays in localStorage.

import type { ThumbRecord } from './thumbBlobs.js';
import type { ImageRecord } from './imageBlobs.js';

/** The localStorage-shaped surface ProjectsStore reads and writes synchronously. */
export interface StorageLike {
  getItem(key: string): string | null;
  setItem(key: string, value: string): void;
  removeItem(key: string): void;
  keys?(): Iterable<string>;
}

/** A payload string, an image or thumbnail record, or an older build's image string. */
export type StoredValue = string | ImageRecord | ThumbRecord;

/** Minimal promise KV over one object store (injectable for tests as an async Map shim). */
export interface PayloadKv {
  get(key: string): Promise<StoredValue | undefined>;
  set(key: string, value: StoredValue): Promise<unknown>;
  remove(key: string): Promise<unknown>;
  entries(): Promise<Array<[string, StoredValue]>>;
}

export interface ProjectsBackend extends StorageLike {
  /** Async IndexedDB write failure — Storage points this at the save-status line. */
  onWriteError: ((err: unknown, key: string) => void) | null;
  keys(): string[];
  /** Re-read one project's payload, image and thumbnail (or the whole mirror) after another tab wrote them. */
  refresh(id?: string | null): Promise<void>;
  /** An image key's data URL, its Blob read back; `keep` (default) holds it while its project is open. */
  materialize(key: string, keep?: boolean): Promise<string | null>;
  /** Resolves once every queued IndexedDB write has settled. */
  flush(): Promise<void>;
  /** The error the key's latest settled write failed with, else null. */
  writeFailure(key: string): unknown;
  /** Whether IndexedDB holds a committed value for the key. */
  isCommitted(key: string): boolean;
}

/** Returns the plain `storage` when IndexedDB is unusable — the pre-IndexedDB behaviour. */
export declare const createProjectsBackend: (opts?: {
  storage?: StorageLike | null;
  idb?: IDBFactory | null;
  kv?: PayloadKv | null;
}) => Promise<ProjectsBackend | StorageLike | null>;
export declare const initProjectsBackend: (opts?: Parameters<typeof createProjectsBackend>[0]) => Promise<ProjectsBackend | StorageLike | null>;
/** Before init (and under node --test) this is plain localStorage. */
export declare const getProjectsBackend: () => ProjectsBackend | StorageLike | null;
