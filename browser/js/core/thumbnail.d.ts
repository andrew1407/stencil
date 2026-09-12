// The projects-list thumbnail: the EDITED result (filter + lines) as a modest JPEG data URL,
// sized for its largest consumer (the hover zoom), since it lives in the localStorage registry.
import type { DrawingApp } from './drawingApp.js';
import type { Storage } from './storage.js';

/** Synchronous, inline render. null with no image or when the canvas refuses (tainted). */
export declare const makeThumbnail: (app: DrawingApp) => string | null;
/** The same render through the image worker (inline when it is unavailable). */
export declare const renderThumbnail: (app: DrawingApp) => Promise<string | null>;

/** An idle-slot handle: requestIdleCallback where it exists, else a setTimeout. */
export type IdleHandle = { idle?: number; timer?: ReturnType<typeof setTimeout> };

export interface ThumbnailScheduler {
  /** Render `projectId`'s thumbnail in idle time; a burst collapses into one render. */
  schedule(projectId: string): void;
  /** Render the pending one NOW, inline — a project switch or an unload cannot wait. */
  flush(): void;
  pending(): boolean;
}

/** The save path's thumbnail scheduler over a Storage instance (store / activeId / app). */
export declare const createThumbnailScheduler: (
  io: Pick<Storage, 'store' | 'activeId' | 'app'>,
  deps?: { idle?: (fn: () => void) => IdleHandle; cancel?: (handle: IdleHandle) => void },
) => ThumbnailScheduler;
