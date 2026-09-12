import type { DrawingApp } from '../core/drawingApp.js';
import type { Project } from './stencilApi.js';

/** stencil.expire(spec) format help, printed when called with no argument. */
export declare const DURATION_HELP: string;

export interface ProjectWrapperDeps {
  app: DrawingApp;
  guard: <T extends object>(obj: T) => T;
  /** The ids this tab currently has open (Project#isOpened). */
  openedIds: () => Set<string>;
}

/** window.stencil's Project wrapper factory: one registry row, or this tab's incognito editor. */
export declare const createProjectWrapper: (
  deps: ProjectWrapperDeps,
) => (id: string | null, incognito?: boolean) => Project;
