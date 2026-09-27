// Project thumbnails, one key per project beside the registry (an IndexedDB Blob through the
// projects backend), so a save rewrites the small registry and never every project's thumbnail.
import type { ProjectMeta, StorageBackend } from './projectsStore.js';

/** stencil_thumb_<id>. */
export declare const THUMB_PREFIX: string;
/** The stored thumbnail as a URL (a data URL, or the backend's object URL), or null. */
export declare const readThumb: (storage: StorageBackend, id: string) => string | null;
/** The meta with its key's thumbnail on it, in place; null stays null. */
export declare const withThumb: <M extends ProjectMeta | null>(storage: StorageBackend, meta: M) => M;
/** A string is stored when it changed; anything else clears the key. QuotaExceededError propagates. */
export declare const writeThumb: (storage: StorageBackend, id: string, thumbnail: string | null | undefined) => void;
/** The registry row for a meta: a string thumbnail becomes null there. */
export declare const rowOf: (meta: ProjectMeta) => ProjectMeta;
/** Moves inline thumbnail strings into their keys, in place; true when a row changed. */
export declare const moveInlineThumbs: (storage: StorageBackend, rows: ProjectMeta[]) => boolean;
export declare const removeThumb: (storage: StorageBackend, id: string) => void;
/** Removes every stencil_thumb_* key. */
export declare const clearThumbs: (storage: StorageBackend) => void;
