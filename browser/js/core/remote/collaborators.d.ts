// Every DrawingApp collaborator, constructed in dependency order. The two view-layer ones
// are injected, so this wiring stays inside the core layer.
import type { DrawingApp } from '../drawingApp.js';
import type { Point } from '../geometry.js';
import type { HistoryStack } from '../historyStack.js';
import type { FormulaEngine } from '../parse/formulaEngine.js';
import type { Renderer } from '../draw/renderer.js';
import type { StrokeFx } from '../line/strokeFx.js';
import type { Storage } from '../storage/storage.js';
import type { ProjectsChangedDetail, TabsCoordinator } from '../launch/tabsCoordinator.js';
import type { ExportService } from '../export/service.js';
import type { SettingsController } from '../settings/controller.js';
import type { ImageModel } from '../image/model.js';
import type { RemoteSyncController } from './syncController.js';
import type { ProjectTransferController } from '../project/transferController.js';
import type { StencilSync } from './stencilSync.js';
import type { InputController } from '../pointer/inputController.js';
import type { PointerController } from '../pointer/controller.js';
import type { ZoomPan } from '../zoom/pan.js';
import type { Emitter } from '../emitter.js';

/** The view-layer constructors and the cross-tab hook the app supplies. */
export interface CollaboratorDeps {
  CoordTable: new (app: DrawingApp) => unknown;
  AccentController: new (app: DrawingApp) => unknown;
  onProjectsChanged: (detail: ProjectsChangedDetail) => void;
}

/** The two ui/ collaborators the app holds (ui/panel/coordTable.js, ui/accent/controller.js). */
export interface CoordTableLike { update(points?: Point[], lineIdx?: number): void; }
export interface AccentControllerLike {
  applyAccent(key: string): string;
  setTheme(theme: string, originEl?: Element | null): void;
  setAccent(key: string, originEl?: Element | null): void;
  setCustomAccent(hex: string, originEl?: Element | null): string | null;
  previewAccent(key: string, originEl?: Element | null): void;
  endAccentPreview(originEl?: Element | null): void;
}

/** The collaborators wireCollaborators sets on the app, in dependency order. */
export interface AppCollaborators {
  /** What an edit changed, by channel (app/changes.js); the control areas subscribe. */
  changes: Emitter;
  history: HistoryStack;
  formula: FormulaEngine;
  renderer: Renderer;
  strokeFx: StrokeFx;
  storage: Storage;
  tabs: TabsCoordinator;
  coordTable: CoordTableLike;
  export: ExportService;
  settings: SettingsController;
  accents: AccentControllerLike;
  imageModel: ImageModel;
  remoteSync: RemoteSyncController;
  projectTransfer: ProjectTransferController;
  stencilSync: StencilSync;
  input: InputController;
  pointer: PointerController;
  zoomPan: ZoomPan;
}

export declare const wireCollaborators: (app: DrawingApp, deps: CollaboratorDeps) => void;
