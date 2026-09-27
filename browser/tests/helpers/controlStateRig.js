// A partial DrawingApp carrying every field the control areas read (js/ui/control/state.js), so the
// real updaters — the draw faces, the file-sync button, the title, the lines list — run under a stub
// DOM. `changes` is a live feed: wireControlState(app) subscribes the areas to it.
import { Emitter } from '../../js/core/emitter.js';

export const makeControlApp = (over = {}) => ({
  image: null, lines: [], isDrawing: false, currentLine: null, undonePoints: [], drawMode: 'line',
  canvas: { width: 0, height: 0, style: { cursor: '' } },
  history: { canUndo: () => false, canRedo: () => false },
  remoteLink: null, activeProjectId: null, imageBaseName: '', nameEditing: false, nameEditor: null,
  storage: { incognito: false, store: { getMeta: () => null } },
  stencilSync: { supported: false, linked: false, liveSync: false, name: '' },
  selectedLineIdx: -1, selectedLines: [], hoverLineIdx: -1,
  changes: new Emitter(),
  compareReadOnly: () => false,
  openInAvailable: () => false,
  updateIncognitoUI() {},
  ...over,
});
