// IndexedDB payload storage behind ProjectsStore's synchronous localStorage-shaped
// contract: an in-memory mirror hydrated once at boot and written through asynchronously.
// Only stencil_project_<id> keys move to IndexedDB; the registry stays in localStorage.

/** The localStorage-shaped surface ProjectsStore reads and writes synchronously. */
export interface StorageLike {
  getItem(key: string): string | null;
  setItem(key: string, value: string): void;
  removeItem(key: string): void;
  keys?(): Iterable<string>;
}

/** Minimal promise KV over one object store (injectable for tests as an async Map shim). */
export interface PayloadKv {
  get(key: string): Promise<string | undefined>;
  set(key: string, value: string): Promise<unknown>;
  remove(key: string): Promise<unknown>;
  entries(): Promise<Array<[string, string]>>;
}

export interface ProjectsBackend extends StorageLike {
  /** Async IndexedDB write failure — Storage points this at the save-status line. */
  onWriteError: ((err: unknown) => void) | null;
  keys(): string[];
  /** Re-read one project's payload (or the whole mirror) after another tab wrote it. */
  refresh(id?: string | null): Promise<void>;
  /** Resolves once every queued IndexedDB write has settled. */
  flush(): Promise<void>;
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
