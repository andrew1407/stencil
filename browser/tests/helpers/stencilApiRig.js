// Shared rig for the stencilApi.test.js family: the inert DOM stubs the facade touches and
// the mock DrawingApp that records every call while really mutating its lines.
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
installDom({
  getElementById: (id) => (id === 'canvas-viewport' ? viewport
    : id === 'chat-transcript' ? chatTranscript
    : id === 'ctx-assist-transcript' ? ctxAssistTranscript
    : null),
  body: { classList: { contains: (c) => c === 'fullscreen-mode' && bodyFullscreen } },
});

export const { createStencil } = await import('../../js/console/stencilApi.js');
export const { hotkeys } = await import('../../js/core/hotkeys.js');
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

    tabs: { onPeers() {}, projectsChanged: rec('projectsChanged') },
    renderer: { redraw: rec('redraw') },
    coordTable: { update: rec('coordTableUpdate') },
    zoomPan: {
      clampScale: (n) => n,
      zoomAroundCenter: rec('zoomAroundCenter'),
      zoomToImagePoint: rec('zoomToImagePoint'),
      fitToWindow: rec('fitToWindow'),
    },
    storage: {
      incognito: false,
      temporary: true,
      save: rec('save'),
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
    setPointCoord(lineIdx, ptIdx, axis, v) {
      calls.push(['setPointCoord', lineIdx, ptIdx, axis, v]);
      const line = lineIdx === -1 ? app.currentLine : app.lines[lineIdx];
      if (line && line.points[ptIdx]) line.points[ptIdx][axis] = Number(v);
    },
    removePoint(lineIdx, ptIdx) {
      calls.push(['removePoint', lineIdx, ptIdx]);
      const line = lineIdx === -1 ? app.currentLine : app.lines[lineIdx];
      if (!line) return;
      line.points.splice(ptIdx, 1);
      if (!line.points.length && lineIdx !== -1) app.lines.splice(lineIdx, 1);
    },
    removeLine(idx) {
      calls.push(['removeLine', idx]);
      app.lines.splice(idx, 1);
      app.saveHistory(); app.renderer.redraw();
    },
    setColor: rec('setColor'), setThickness: rec('setThickness'), setPointSize: rec('setPointSize'),
    setLineStyle: rec('setLineStyle'), setShowPoints: rec('setShowPoints'), setShowLines: rec('setShowLines'),
    setImageFilter: rec('setImageFilter'), setFilterColor: rec('setFilterColor'), setUnit: rec('setUnit'),
    setPageSize: rec('setPageSize'), setCustomPageWidth: rec('setCustomPageWidth'), setCustomPageHeight: rec('setCustomPageHeight'),
    setTheme: rec('setTheme'), setDrawMode: rec('setDrawMode'), setHoldDrawDelay: rec('setHoldDrawDelay'), setAllowFormulas: rec('setAllowFormulas'),
    setAccent(key) { calls.push(['setAccent', key]); app.accent = key; app.customAccent = null; },
    setCustomAccent(hex) { calls.push(['setCustomAccent', hex]); app.customAccent = hex; return hex; },
    setFormula: rec('setFormula'), setVisualColor: rec('setVisualColor'), setTooltipOption: rec('setTooltipOption'),
    rotateImage: rec('rotateImage'), undo: rec('undo'), redo: rec('redo'),
    flipSelectedLine: rec('flipSelectedLine'), rotateSelectedLineQuarter: rec('rotateSelectedLineQuarter'),
    startDrawingMode: rec('startDrawingMode'), stopDrawingMode: rec('stopDrawingMode'),
    clearAllLines: rec('clearAllLines'), saveImage: rec('saveImage'),
    copyLayoutToClipboard: rec('copyLayoutToClipboard'), copyImageToClipboard: rec('copyImageToClipboard'),
    downloadJSON: rec('downloadJSON'), applyPastedLayout: rec('applyPastedLayout'),
    installLayout: rec('installLayout'),
    newEditor: rec('newEditor'), updateIncognitoUI: rec('updateIncognitoUI'),
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
    pixelToPageCoords(x, y) { calls.push(['pixelToPageCoords', x, y]); return { x: x / 10, y: y / 10 }; },
    getPageDimensions: () => ({ width: 20, height: 30 }),
    canvas: { width: 200, height: 300 },
    createBlankImage(opts) { calls.push(['createBlankImage', opts]); app.image = { width: opts.width || 100, height: opts.height || 100 }; },
    isDrawing: false,
    ...over,
  };
  // Collaborator namespaces mirror the real DrawingApp (app.<collab>.<method>()), each delegating to
  // the flat recorded method so lastCall(app, name) assertions still hold.
  const delegate = (names) => Object.fromEntries(names.map((n) => [n, (...a) => app[n]?.(...a)]));
  app.settings = delegate(['setColor', 'setThickness', 'setPointSize', 'setLineStyle', 'setShowPoints',
    'setShowLines', 'setImageFilter', 'setFilterColor', 'setPageSize', 'setCustomPageWidth',
    'setCustomPageHeight', 'setUnit', 'setAllowFormulas', 'setFormula', 'setTooltipOption', 'setVisualColor']);
  app.export = delegate(['saveImage', 'shareImage', 'downloadJSON', 'uploadJSON',
    'copyImageToClipboard', 'copyLayoutToClipboard', 'applyPastedLayout', 'installLayout']);
  app.imageModel = delegate(['defaultCropRect', 'effectiveOriginalDims', 'effectiveOriginalDataUrl',
    'rebuildCroppedImage', 'rotateImage', 'applyCrop']);
  app.remoteSync = delegate(['scheduleRemoteSync', 'onServerProjectEvent', 'reloadRemoteActive', 'saveToServer']);
  app.input = delegate(['holdAnchorPoint', 'setHoldDrawDelay']);
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
