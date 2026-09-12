// Window-side bridge over the DOM-free ProjectsStore for the ACTIVE project: builds the
// payload from live app state, reads payloads back into the DOM, and owns the
// temporary / incognito flags. save() is a no-op (with a throttled hint) in temp mode.
import type { DrawingApp } from './drawingApp.js';
import type { ProjectsStore, ProjectPayload } from './projectsStore.js';

export declare class Storage {
  constructor(app: DrawingApp);
  app: DrawingApp;
  store: ProjectsStore;
  activeId: string | null;
  temporary: boolean;
  /** A deliberately unsaved editor: never persists, even once an image loads. */
  incognito: boolean;
  /** The trailing-window save; flush() forces one now. */
  saveSoon: { (): void; flush(): void };
  save(): void;
  /** Re-read the active project after another tab saved it (light path when the image is unchanged). */
  syncActiveFromStorage(): void;
  showImageMissingBanner(show: boolean): void;
  /** Boot-time: migrate legacy keys + sweep expired projects; loads nothing. */
  restore(): void;
  loadProject(id: string): boolean;
  loadPayloadIntoApp(payload: ProjectPayload | null | undefined, opts?: { landing?: boolean }): void;
  newTemporary(opts?: { keepChat?: boolean }): void;
  /** A temp editor got its first image: allocate an id; the caller then save()s. */
  promoteTemporaryToProject(): string;
}
