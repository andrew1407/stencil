// Every DrawingApp collaborator, constructed in dependency order. The two view-layer ones
// are injected, so this wiring stays inside the core layer.
import type { DrawingApp } from '../drawingApp.js';
import type { ProjectsChangedDetail } from '../launch/tabsCoordinator.js';

/** The view-layer constructors and the cross-tab hook the app supplies. */
export interface CollaboratorDeps {
  CoordTable: new (app: DrawingApp) => unknown;
  AccentController: new (app: DrawingApp) => unknown;
  onProjectsChanged: (detail: ProjectsChangedDetail) => void;
}

export declare const wireCollaborators: (app: DrawingApp, deps: CollaboratorDeps) => void;
