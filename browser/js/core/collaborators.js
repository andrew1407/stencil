// Every DrawingApp collaborator, constructed in dependency order. The two view-layer ones are
// injected, so this wiring stays DOM-free; each takes the app and knows none of the others.
import { HistoryStack } from './historyStack.js';
import { FormulaEngine } from './formulaEngine.js';
import { Renderer } from './renderer.js';
import { StrokeFx } from './strokeFx.js';
import { Storage } from './storage.js';
import { TabsCoordinator } from './tabsCoordinator.js';
import { ZoomPan } from './zoomPan.js';
import { ExportService } from './exportService.js';
import { SettingsController } from './settingsController.js';
import { ImageModel } from './imageModel.js';
import { RemoteSyncController } from './remoteSyncController.js';
import { ProjectTransferController } from './projectTransferController.js';
import { InputController } from './inputController.js';
import { PointerController } from './pointerController.js';
import { StencilSync } from './stencilSync.js';
import { publish, EVENTS } from '../eventBus/appBus.js';

// `host` is the narrow slice of app state/callbacks the transfer controller needs;
// `getConnections` is a getter because stencilApi creates the manager lazily.
const transferHost = (app) => ({
  get activeProjectId() { return app.activeProjectId; },
  set activeProjectId(id) { app.activeProjectId = id; },
  get remoteLink() { return app.remoteLink; },
  set remoteLink(link) { app.remoteLink = link; },
  set blankColor(color) { app.blankColor = color; },
  set imageBaseName(name) { app.imageBaseName = name; },
  get chatPersistence() { return app.chatPersistence; },
  updateProjectTitle: (force) => app.updateProjectTitle(force),
  updateIncognitoUI: () => app.updateIncognitoUI(),
  newEditor: (opts) => app.newEditor(opts),
  loadImageFromFile: (file, opts) => app.loadImageFromFile(file, opts),
  setBlankColor: (color) => app.setBlankColor(color),
});

export const wireCollaborators = (app, { CoordTable, AccentController, onProjectsChanged }) => {
  app.history = new HistoryStack();
  app.formula = new FormulaEngine();
  app.renderer = new Renderer(app);
// Vertices in flight: every route that adds a point hands it here (strokeFx.js).
  app.strokeFx = new StrokeFx(app);
  app.storage = new Storage(app);
  app.tabs = new TabsCoordinator();
  app.tabs.onProjectsChanged(detail => onProjectsChanged(detail || {}));
// A peer's accent change repaints live (no re-broadcast); a local custom accent wins.
  app.tabs.onAccent(key => {
    if (app.customAccent) return;
    const next = app.accents.applyAccent(key);
    publish(EVENTS.accentChanged, next);
  });
  app.coordTable = new CoordTable(app);
  app.export = new ExportService(app);
  app.settings = new SettingsController(app);
  app.accents = new AccentController(app);
  app.imageModel = new ImageModel(app);
  app.remoteSync = new RemoteSyncController(app);
  app.projectTransfer = new ProjectTransferController({
    storage: app.storage,
    tabs: app.tabs,
    remoteSync: app.remoteSync,
    getConnections: () => app.connections,
    host: transferHost(app),
  });
// Opt-in live sync with a .stencil on disk (File System Access / Chromium only).
  app.stencilSync = new StencilSync(app);
  app.input = new InputController(app);
  app.pointer = new PointerController(app);
// <stencil-tooltip> owns its render logic; aliased as tooltipMgr for existing callers.
  app.tooltip.app = app;
  app.tooltipMgr = app.tooltip;
  app.zoomPan = new ZoomPan(app);
};
