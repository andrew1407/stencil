// Another tab changed the project set: what the editor does when the project it is showing
// is removed, cleared, closed, or saved again by a peer.
import type { DrawingApp } from './drawingApp.js';
import type { ProjectsChangedDetail } from './tabsCoordinator.js';

/** A removal/clear/close tears the editor down; an update re-reads it while idle. */
export declare const onRemoteProjectsChange: (app: DrawingApp, detail: ProjectsChangedDetail) => void;
