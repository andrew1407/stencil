// The active-project session over ProjectsStore: attaching the store, the boot sweep, the
// refresh-on-open snap, promoting a temporary editor, and clearing the editor.
import type { DrawingApp } from '../drawingApp.js';
import type { ProjectsStore } from '../project/store/projectsStore.js';
import type { Storage } from './storage.js';

/** The store over the IndexedDB-mirroring backend, with its write-error hint wired. */
export declare const attachProjectsStore: (storage: Storage) => ProjectsStore;
/** Boot-time legacy migration + expiry sweep; loads nothing. */
export declare const restoreProjects: (storage: Storage) => void;
/** expiresAt = now + its refresh period; keep-forever (0) is left alone. */
export declare const autoRefreshOnOpen: (storage: Storage, id: string) => void;
/** Allocates the project id; the caller then save()s. */
export declare const promoteTemporary: (storage: Storage) => string;
/** Everything the emptied editor forgets; the canvas itself is the caller's to collapse. */
export declare const clearEditorState: (app: DrawingApp) => void;
