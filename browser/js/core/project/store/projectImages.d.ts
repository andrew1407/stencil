// A project's image under its own key beside its payload: a save re-serialises the small layout and
// rewrites the image only when it changed; an older build's inline image still loads.
import type { ProjectPayload, StorageBackend } from './projectsStore.js';

/** stencil_image_<id>. */
export declare const IMAGE_PREFIX: string;
/** The stored image: a data URL, or an object URL from an IndexedDB backend; null when none. */
export declare const readImage: (storage: StorageBackend, id: string) => string | null;
/** A string is stored when it differs from the stored one; anything else clears the key. QuotaExceededError propagates. */
export declare const writeImage: (storage: StorageBackend, id: string, image: string | null | undefined) => void;
/** The payload as its key stores it: every field but `image`. */
export declare const withoutImage: <P extends Partial<ProjectPayload>>(payload: P | null | undefined) => Omit<P, 'image'>;
/** The payload as callers read it: an inline image as it is, else the image key's. */
export declare const withImage: <P extends object>(storage: StorageBackend, id: string, payload: P) => P & { image: string | null };
/** The image as a data URL: a backend's object URL read back (held while open when `keep`). */
export declare const resolveImage: (storage: StorageBackend, id: string, image: string | null | undefined, keep?: boolean) => Promise<string | null>;
export declare const removeImage: (storage: StorageBackend, id: string) => void;
/** Removes every stencil_image_* key. */
export declare const clearImages: (storage: StorageBackend) => void;
