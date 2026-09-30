// Shared rig for the stencilApi.test.js family: the inert DOM stubs the facade touches and
// the mock DrawingApp that records every call while really mutating its lines. The core
// functions the facade calls (line edits, draw mode, page metrics, the load and blank flows) run
// for real against it; their collaborators record.
import { installDom, createStubElement } from './dom.js';

// Inert DOM stubs so the few document/window-touching paths (the viewport pan, color
// canvas, the links-modal refresh event) stay no-ops instead of throwing under node --test.
globalThis.window = globalThis.window ?? {};
globalThis.window.dispatchEvent = globalThis.window.dispatchEvent ?? (() => {});
// A mutable stub viewport (stencil.move pans it) + a body whose fullscreen class is
// driven by a flag the toggleFullscreen mock flips, so move/fullscreen are observable.
export const viewport = { scrollLeft: 0, scrollTop: 0 };
let bodyFullscreen = false;
// The two transcripts stencil.chat.swapSides restamps — real classList.toggle so the
// applied class is observable.
export const chatTranscript = createStubElement();
export const ctxAssistTranscript = createStubElement();
// The blank flow paints its fill on a scratch canvas: record the size and fill, answer a PNG blob.
export const blankCanvases = [];
const scratchCanvas = () => {
  const cnv = { width: 0, height: 0, fills: [] };
  const ctx = { fillStyle: '', fillRect: (x, y, w, h) => cnv.fills.push([ctx.fillStyle, x, y, w, h]) };
  cnv.getContext = () => ctx;
  cnv.toBlob = (cb, type) => cb(new Blob(['png'], { type }));
  blankCanvases.push(cnv);
  return cnv;
};
installDom({
  getElementById: (id) => (id === 'canvas-viewport' ? viewport
    : id === 'chat-transcript' ? chatTranscript
    : id === 'ctx-assist-transcript' ? ctxAssistTranscript
    : null),
  body: { classList: { contains: (c) => c === 'fullscreen-mode' && bodyFullscreen } },
  createElement: (tag) => (tag === 'canvas' ? scratchCanvas() : createStubElement(tag)),
});

export const { createStencil } = await import('../../js/console/stencilApi.js');
export const { hotkeys } = await import('../../js/core/settings/hotkeys.js');
export const { validateLayout } = await import('../../js/core/layout.js');

// The mock records every call as [name, ...args] in `app.calls`; line/point mutators really mutate
// app.lines, so wrapper effects (move/rotate/add/remove) can be asserted.
export const makeApp = (over = {}) => {
  const calls = [];
  const rec = (name) => (...args) => { calls.push([name, ...args]); };
  const app = {
    calls,
    activeProjectId: null,
    lines: [],
    currentLine: null,
    coordLineIdx: -1,
    selectedLineIdx: -1, selectedLines: [], focusedPtIdx: -1, hoveredPtIdx: -1,
    hoverPt: null, hoverLineIdx: -1, listHoverLineIdx: -1,
    image: null,
    originalImage: null,
    scale: 1,
    // settings backing fields
    color: '#ff0000', thickness: 2, pointSize: 5, style: 'solid',
    showPoints: true, showLines: true, imageFilter: 'none', filterColor: '#000000',
    unit: 'cm', pageSize: 'A4', customPageWidth: 21, customPageHeight: 29.7,
    theme: 'dark', drawMode: 'line', holdDrawDelay: 500, allowFormulas: false, formulaX: '', formulaY: '',
    accent: 'violet', customAccent: null,
    defaultFillColor: '#ffffff', selGlowColor: '#000000', hoverRingColor: '#000000', focusRingColor: '#000000',
    // tooltip + provenance backing fields
    tooltipEnabled: true, tooltipShowPage: true, tooltipShowScreen: true, tooltipShowCoords: true,
    imageBaseName: 'pic.png', imageSource: null, imageResource: null,

    tabs: { onPeers() {}, projectsChanged: rec('projectsChanged'), reportActive: rec('reportActive'),
      reportIncognito: rec('reportIncognito') },
    renderer: { redraw: rec('redraw'), requestRedraw: rec('requestRedraw') },
    coordTable: { update: rec('coordTableUpdate') },
    zoomPan: {
      clampScale: (n) => n,
      zoomAroundCenter: rec('zoomAroundCenter'),
      zoomToImagePoint: rec('zoomToImagePoint'),
      fitToWindow: rec('fitToWindow'),
      syncViewportHeight: rec('syncViewportHeight'),
    },
    storage: {
      incognito: false,
      temporary: true,
      save: rec('save'),
      saveSoon: rec('saveSoon'),
      newTemporary: rec('newTemporary'),
      promoteTemporaryToProject: rec('promoteTemporaryToProject'),
      store: {
        list: () => app._metas,
        getMeta: (id) => app._metas.find((m) => m.id === id) || null,
        get: (id) => app._projects[id] || null,
        nameExists: (name, exceptId) => app._metas.some((m) => m.name === name && m.id !== exceptId),
        upsert: rec('upsert'),
        expiresAt: () => null,
        isExpired: () => false,
      },
    },
    _metas: [],
    _projects: {},

    saveHistory: rec('saveHistory'),
    compareReadOnly: () => false,
    deselectLine: rec('deselectLine'),
    hideSelectionPanels: rec('hideSelectionPanels'),
    syncDrawModeUI: rec('syncDrawModeUI'),
    setColor: rec('setColor'), setThickness: rec('setThickness'), setPointSize: rec('setPointSize'),
    setLineStyle: rec('setLineStyle'), setShowPoints: rec('setShowPoints'), setShowLines: rec('setShowLines'),
    setImageFilter: rec('setImageFilter'), setFilterColor: rec('setFilterColor'), setUnit: rec('setUnit'),
    setFilter: rec('setFilter'), filterStep: (fn) => fn(),
    setPageSize: rec('setPageSize'), setCustomPageWidth: rec('setCustomPageWidth'), setCustomPageHeight: rec('setCustomPageHeight'),
    setTheme: rec('setTheme'), setHoldDrawDelay: rec('setHoldDrawDelay'), setAllowFormulas: rec('setAllowFormulas'),
    setAccent(key) { calls.push(['setAccent', key]); app.accent = key; app.customAccent = null; },
    setCustomAccent(hex) { calls.push(['setCustomAccent', hex]); app.customAccent = hex; return hex; },
    setFormula: rec('setFormula'), setVisualColor: rec('setVisualColor'), setTooltipOption: rec('setTooltipOption'),
    rotateImage: rec('rotateImage'), undo: rec('undo'), redo: rec('redo'),
    clearAllLines: rec('clearAllLines'), saveImage: rec('saveImage'),
    copyLayoutToClipboard: rec('copyLayoutToClipboard'), copyImageToClipboard: rec('copyImageToClipboard'),
    downloadJSON: rec('downloadJSON'), applyPastedLayout: rec('applyPastedLayout'),
    installLayout: rec('installLayout'),
    updateIncognitoUI: rec('updateIncognitoUI'),
    renameProject: rec('renameProject'), renewProject: rec('renewProject'),
    setProjectExpiration(id, opts) {
      calls.push(['setProjectExpiration', id, opts]);
      const m = app._metas.find((x) => x.id === id);
      if (m) Object.assign(m, opts);
      return m || null;
    },
    setProjectColor(id, color) {
      calls.push(['setProjectColor', id, color]);
      const m = app._metas.find((x) => x.id === id);
      if (m) m.color = color;
      return m || null;
    },
    setProjectKeywords(id, keywords) {
      calls.push(['setProjectKeywords', id, keywords]);
      const m = app._metas.find((x) => x.id === id);
      if (m) m.keywords = keywords;
      return m || null;
    },
    setProjectDescription(id, description) {
      calls.push(['setProjectDescription', id, description]);
      const m = app._metas.find((x) => x.id === id);
      if (m) m.description = description;
      return m || null;
    },
    setProjectBlankColor(id, color) {
      calls.push(['setProjectBlankColor', id, color]);
      const m = app._metas.find((x) => x.id === id);
      if (!m || !m.blank) return null;   // no-op for non-blank (matches the real app)
      m.blankColor = color;
      return m;
    },
    closeProject: rec('closeProject'), switchToProject: rec('switchToProject'),
    toggleFullscreen() { calls.push(['toggleFullscreen']); bodyFullscreen = !bodyFullscreen; },
    canvas: { width: 200, height: 300 },
    isDrawing: false,
    ...over,
  };
  // Collaborator namespaces mirror the real DrawingApp (app.<collab>.<method>()), each delegating to
  // the flat recorded method so lastCall(app, name) assertions still hold.
  const delegate = (names) => Object.fromEntries(names.map((n) => [n, (...a) => app[n]?.(...a)]));
  app.settings = delegate(['setColor', 'setThickness', 'setPointSize', 'setLineStyle', 'setShowPoints',
    'setShowLines', 'setImageFilter', 'setFilterColor', 'setFilter', 'filterStep', 'setPageSize', 'setCustomPageWidth',
    'setCustomPageHeight', 'setUnit', 'setAllowFormulas', 'setFormula', 'setTooltipOption', 'setVisualColor']);
  app.export = delegate(['saveImage', 'shareImage', 'downloadJSON', 'uploadJSON',
    'copyImageToClipboard', 'copyLayoutToClipboard', 'applyPastedLayout', 'installLayout']);
  app.imageModel = delegate(['defaultCropRect', 'effectiveOriginalDims', 'effectiveOriginalDataUrl',
    'rebuildCroppedImage', 'rotateImage', 'applyCrop']);
  app.remoteSync = delegate(['scheduleRemoteSync', 'onServerProjectEvent', 'reloadRemoteActive', 'saveToServer']);
  app.input = delegate(['holdAnchorPoint', 'setHoldDrawDelay']);
  app.accents = delegate(['setTheme', 'setAccent', 'setCustomAccent', 'previewAccent', 'endAccentPreview']);
  app.projectTransfer = delegate(['openRemoteProject', 'switchToProject', 'openProjectInNewTab',
    'openRemoteProjectInNewTab', 'clearAllProjects', 'renewProject', 'setProjectExpiration', 'renameProject',
    'setProjectColor', 'setProjectKeywords', 'setProjectDescription', 'setProjectBlankColor', 'removeProject',
    'moveProjectToServer', 'copyProjectToServer', 'moveProjectToLocal', 'copyServerProjectToLocal',
    'copyServerProjectToIncognito', 'copyProject']);
  return app;
};

export const called = (app, name) => app.calls.filter((c) => c[0] === name);
export const lastCall = (app, name) => called(app, name).at(-1);

// A two-project app with the first one active.
export const withProjects = () => makeApp({
  activeProjectId: 1,
  _metas: [{ id: 1, name: 'Alpha' }, { id: 2, name: 'Beta' }],
  _projects: {
    1: { meta: { id: 1, name: 'Alpha' }, payload: { layout: {} } },
    2: { meta: { id: 2, name: 'Beta' }, payload: { layout: {} } },
  },
});
