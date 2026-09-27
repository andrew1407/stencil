// The localStorage-shaped plumbing under ProjectsStore: key enumeration, guarded JSON
// read/write, the registry row default-fill, and the project-only wipe.
import type { ProjectMeta, StorageBackend } from './projectsStore.js';

/** keys() (the test shim) is preferred over Object.keys. */
export declare const storageKeys: (storage: StorageBackend | null | undefined) => string[];
/** `fallback` for a null string, junk JSON or a stored null. */
export declare const parseJSON: <T>(raw: string | null | undefined, fallback: T) => T;
/** `fallback` on a missing key, junk JSON or a stored null. */
export declare const readJSON: <T>(storage: StorageBackend, key: string, fallback: T) => T;
/** QuotaExceededError propagates so the DOM adapter can evict + retry. */
export declare const writeJSON: (storage: StorageBackend, key: string, value: unknown) => void;
/** Fills only ABSENT fields, so an explicit expiresAt of 0 (keep forever) survives. */
export declare const normalizeMeta: (m: ProjectMeta | null | undefined) => ProjectMeta | null | undefined;
/** A JSON round trip of plain data without the parse: undefined fields dropped, undefined or non-finite elements null. */
export declare const cloneJson: <T>(v: T) => T;
/** The normalized rows a read of `JSON.stringify(arr)` parses to. */
export declare const registryRows: (arr: readonly (ProjectMeta | null)[]) => (ProjectMeta | null)[];
/** Removes every payload key plus the registry; never a global drawingApp_* key. */
export declare const clearProjectKeys: (storage: StorageBackend, registryKey: string, prefix: string) => void;
