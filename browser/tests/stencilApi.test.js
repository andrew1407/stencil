// Tests for the editor's console control API — window.stencil (js/console/stencilApi.js).
// createStencil(app) is a closure facade over the live DrawingApp: every method routes
// through the same core app.* methods the toolbar uses. We drive it with a mock `app`
// that records calls (and mutates its lines/points so move/apply effects are observable),
// so these tests pin the facade's behaviour without a browser: the read-only guard, the
// chainable editor actions, line/point/project wrappers, settings flattening, zoom, crop,
// the incognito guard rule, and shortcut rebinding (against the real hotkeys singleton).

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { installFetchStub } from './helpers/fetchStub.js';
import { installDom, createStubElement } from './helpers/dom.js';

// Inert DOM stubs so the few document/window-touching paths (closeModals, color canvas,
// the links-modal refresh event) stay no-ops instead of throwing under node --test.
globalThis.window = globalThis.window ?? {};
globalThis.window.dispatchEvent = globalThis.window.dispatchEvent ?? (() => {});
// A mutable stub viewport (stencil.move pans it) + a body whose fullscreen class is
// driven by a flag the toggleFullscreen mock flips, so move/fullscreen are observable.
const viewport = { scrollLeft: 0, scrollTop: 0 };
let bodyFullscreen = false;
// The two transcripts stencil.chat.swapSides restamps — real classList.toggle so the
// applied class is observable.
const chatTranscript = createStubElement();
const ctxAssistTranscript = createStubElement();
installDom({
  getElementById: (id) => (id === 'canvas-viewport' ? viewport
    : id === 'chat-transcript' ? chatTranscript
    : id === 'ctx-assist-transcript' ? ctxAssistTranscript
    : null),
  body: { classList: { contains: (c) => c === 'fullscreen-mode' && bodyFullscreen } },
});

const { createStencil } = await import('../js/console/stencilApi.js');
const { hotkeys } = await import('../js/core/hotkeys.js');
const { validateLayout } = await import('../js/core/layout.js');

// ── Mock DrawingApp ──────────────────────────────────────────────────────────
// Records every call as [name, ...args] in `app.calls`. Line/point mutators actually
// mutate app.lines so wrapper effects (move/rotate/add/remove) can be asserted.
const makeApp = (over = {}) => {
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
  // Collaborator namespaces mirror the real DrawingApp: the facade routes settings / export /
  // image-geometry / remote-sync / input calls through app.<collab>.<method>(). Delegate each to
  // the flat recorded (or per-test overridden) method so lastCall(app, name) assertions still hold.
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

const called = (app, name) => app.calls.filter((c) => c[0] === name);
const lastCall = (app, name) => called(app, name).at(-1);

// ── Read-only guard ────────────────────────────────────────────────────────────
test('guard: reassigning a method, defining, or deleting a member throws; real setters write through', () => {
  const app = makeApp();
  const stencil = createStencil(app);

  assert.throws(() => { stencil.undo = 0; }, /read-only/);
  assert.throws(() => { stencil.getProjectByName = 0; }, /read-only/);
  assert.throws(() => { delete stencil.lines; }, /cannot be deleted/);
  assert.throws(() => { Object.defineProperty(stencil, 'x', { value: 1 }); }, /read-only/);

  // A legit setter (a flattened setting) writes through to the app.
  stencil.lineColor = '#ABC';
  assert.deepEqual(lastCall(app, 'setColor'), ['setColor', '#aabbcc']); // #abc → #aabbcc via toHexColor
});

// ── Chainable editor actions ────────────────────────────────────────────────────
test('editor actions route to app.* and return the facade for chaining', () => {
  const app = makeApp();
  const stencil = createStencil(app);

  assert.equal(stencil.rotateLeft(), stencil);
  assert.deepEqual(lastCall(app, 'rotateImage'), ['rotateImage', -1]);
  assert.equal(stencil.rotateRight(), stencil);
  assert.deepEqual(lastCall(app, 'rotateImage'), ['rotateImage', 1]);

  // A whole chain hits each underlying method once, in order.
  stencil.undo().redo().clearLines().startDrawing().stopDrawing()
    .downloadImage().copyImage().copyLayout().downloadLayout().newEditor().zoomFit();
  for (const m of ['undo', 'redo', 'clearAllLines', 'startDrawingMode', 'stopDrawingMode',
    'saveImage', 'copyImageToClipboard', 'copyLayoutToClipboard', 'downloadJSON', 'newEditor', 'fitToWindow'])
    assert.equal(called(app, m).length, 1, `${m} called once`);
});

test('drawing get/set mirrors the start/stop buttons and honours the loaded-image guard', () => {
  const app = makeApp();
  const stencil = createStencil(app);

  // No image → enabling drawing is a no-op (matches the toolbar guard).
  stencil.drawing = true;
  assert.equal(called(app, 'startDrawingMode').length, 0);

  app.image = { width: 10, height: 10 };
  stencil.drawing = true;
  assert.equal(called(app, 'startDrawingMode').length, 1);

  app.isDrawing = true;
  stencil.drawing = false;
  assert.equal(called(app, 'stopDrawingMode').length, 1);
});

// ── Settings flattening ─────────────────────────────────────────────────────────
test('settings flatten onto the facade and onto stencil.settings, both driving app setters', () => {
  const app = makeApp();
  const stencil = createStencil(app);

  assert.equal(stencil.thickness, 2);          // reads app.thickness
  stencil.thickness = 4;
  assert.deepEqual(lastCall(app, 'setThickness'), ['setThickness', 4]);

  stencil.settings.pageSize = 'a3';
  assert.deepEqual(lastCall(app, 'setPageSize'), ['setPageSize', 'a3']);

  stencil.darkTheme = false;
  assert.deepEqual(lastCall(app, 'setTheme'), ['setTheme', 'light']);
  stencil.darkTheme = true;
  assert.deepEqual(lastCall(app, 'setTheme'), ['setTheme', 'dark']);
});

// ── Lines ───────────────────────────────────────────────────────────────────────
test('line.move translates every point and commits (history + redraw + coord table)', () => {
  const app = makeApp({ lines: [{ color: '#111111', thickness: 1, points: [{ x: 10, y: 20 }, { x: 30, y: 40 }] }] });
  const stencil = createStencil(app);

  const line = stencil.lines[0];
  assert.equal(line.move({ x: 5, y: -5 }), line);
  assert.deepEqual(app.lines[0].points, [{ x: 15, y: 15 }, { x: 35, y: 35 }]);
  assert.equal(called(app, 'saveHistory').length, 1);
  assert.equal(called(app, 'redraw').length, 1);
  assert.equal(called(app, 'coordTableUpdate').length, 1);
});

test('line.rotate rotates points around the bbox centre by default', () => {
  const app = makeApp({ lines: [{ points: [{ x: 0, y: 0 }, { x: 10, y: 0 }] }] });
  const stencil = createStencil(app);

  stencil.lines[0].rotate(90);   // centre = (5,0); 90° CW
  const [p0, p1] = app.lines[0].points;
  assert.ok(Math.abs(p0.x - 5) < 1e-9 && Math.abs(p0.y - (-5)) < 1e-9);
  assert.ok(Math.abs(p1.x - 5) < 1e-9 && Math.abs(p1.y - 5) < 1e-9);
});

test('flipH/flipV/rotate90/rotateMinus90 route to the selected-line transforms (chainable)', () => {
  const app = makeApp();
  const stencil = createStencil(app);

  assert.equal(stencil.flipH(), stencil);
  assert.deepEqual(lastCall(app, 'flipSelectedLine'), ['flipSelectedLine', true]);
  assert.equal(stencil.flipV(), stencil);
  assert.deepEqual(lastCall(app, 'flipSelectedLine'), ['flipSelectedLine', false]);
  assert.equal(stencil.rotate90(), stencil);
  assert.deepEqual(lastCall(app, 'rotateSelectedLineQuarter'), ['rotateSelectedLineQuarter', 1]);
  assert.equal(stencil.rotateMinus90(), stencil);
  assert.deepEqual(lastCall(app, 'rotateSelectedLineQuarter'), ['rotateSelectedLineQuarter', -1]);
});

test('line.apply batch-updates style props and normalizes color/fillColor', () => {
  const app = makeApp({ lines: [{ color: '#000000', thickness: 1, pointSize: 1, style: 'solid', fillColor: 'transparent', points: [] }] });
  const stencil = createStencil(app);

  stencil.lines[0].apply({ color: '#ABC', thickness: 3, pointSize: 7, style: 'dashed', fillColor: 'transparent' });
  const l = app.lines[0];
  assert.equal(l.color, '#aabbcc');
  assert.equal(l.thickness, 3);
  assert.equal(l.pointSize, 7);   // pointSize alias → pointSize
  assert.equal(l.style, 'dashed');
  assert.equal(l.fillColor, 'transparent');
});

test('line.add inserts a point at a neighbour slot; line.remove(index) drops one', () => {
  const app = makeApp({ lines: [{ points: [{ x: 0, y: 0 }, { x: 10, y: 10 }] }] });
  const stencil = createStencil(app);

  stencil.lines[0].add({ x: 5, y: 5 }, { neighbour: 0 });   // after index 0
  assert.deepEqual(app.lines[0].points, [{ x: 0, y: 0 }, { x: 5, y: 5 }, { x: 10, y: 10 }]);

  stencil.lines[0].remove(1);
  assert.deepEqual(lastCall(app, 'removePoint'), ['removePoint', 0, 1]);
  assert.deepEqual(app.lines[0].points, [{ x: 0, y: 0 }, { x: 10, y: 10 }]);
});

test('line.join appends the other line\'s points and drops the other line', () => {
  const app = makeApp({ lines: [
    { points: [{ x: 0, y: 0 }] },
    { points: [{ x: 9, y: 9 }, { x: 8, y: 8 }] },
  ] });
  const stencil = createStencil(app);

  const lines = stencil.lines;
  lines[0].join(lines[1]);
  assert.equal(app.lines.length, 1);
  assert.deepEqual(app.lines[0].points, [{ x: 0, y: 0 }, { x: 9, y: 9 }, { x: 8, y: 8 }]);
  assert.deepEqual(lastCall(app, 'removeLine'), ['removeLine', 1]);
});

// ── Points ──────────────────────────────────────────────────────────────────────
test('points getter targets the current line; point.apply/move route through setPointCoord', () => {
  const app = makeApp({ currentLine: { points: [{ x: 100, y: 100 }] } });
  const stencil = createStencil(app);

  const pt = stencil.points[0];
  assert.equal(pt.x, 100);
  assert.equal(pt.y, 100);

  pt.apply({ x: 5, y: 6 });
  assert.deepEqual(app.currentLine.points[0], { x: 5, y: 6 });

  pt.move({ x: 10 });   // relative: 5 + 10
  assert.equal(app.currentLine.points[0].x, 15);
});

// ── Projects ──────────────────────────────────────────────────────────────────────
const withProjects = () => makeApp({
  activeProjectId: 1,
  _metas: [{ id: 1, name: 'Alpha' }, { id: 2, name: 'Beta' }],
  _projects: {
    1: { meta: { id: 1, name: 'Alpha' }, payload: { layout: {} } },
    2: { meta: { id: 2, name: 'Beta' }, payload: { layout: {} } },
  },
});

test('getProjectByName matches case-insensitively; current wraps the active project', () => {
  const app = withProjects();
  const stencil = createStencil(app);

  assert.equal(stencil.getProjectByName('alpha').id, 1);
  assert.equal(stencil.getProjectByName('  BETA  ').id, 2);
  assert.equal(stencil.getProjectByName('nope'), null);
  assert.equal(stencil.current.id, 1);
});

test('project.name setter validates empty + duplicate names and routes to renameProject', () => {
  const app = withProjects();
  app.renameProject = (id, name) => { app.calls.push(['renameProject', id, name]); return true; };
  const stencil = createStencil(app);

  const p = stencil.getProjectByName('alpha');
  assert.throws(() => { p.name = '   '; }, /cannot be empty/);
  assert.throws(() => { p.name = 'Beta'; }, /already exists/);   // collides with id 2
  p.name = 'Gamma';
  assert.deepEqual(lastCall(app, 'renameProject'), ['renameProject', 1, 'Gamma']);
});

test('stencil.projectColor reads the active meta colour and validates on set', () => {
  const app = withProjects();
  app._metas = [{ id: 1, name: 'Alpha', color: '#ec4899' }, { id: 2, name: 'Beta' }];
  const stencil = createStencil(app);

  // Getter: active project's colour, or '' when unset.
  assert.equal(stencil.projectColor, '#ec4899');
  app._metas[0].color = '';
  assert.equal(stencil.projectColor, '');

  // Setter: a valid hex routes to setProjectColor; junk throws; '' clears.
  stencil.projectColor = '#0EA5E9';
  assert.deepEqual(lastCall(app, 'setProjectColor'), ['setProjectColor', 1, '#0EA5E9']);
  stencil.projectColor = '';
  assert.deepEqual(lastCall(app, 'setProjectColor'), ['setProjectColor', 1, '']);
  assert.throws(() => { stencil.projectColor = 'not-a-color'; }, /Invalid project color/);
});

test('stencil.projectColor throws with no active project', () => {
  const app = makeApp();   // activeProjectId null
  const stencil = createStencil(app);
  assert.equal(stencil.projectColor, '');
  assert.throws(() => { stencil.projectColor = '#fff'; }, /No active project/);
});

test('project.color get/set validates hex and routes to setProjectColor', () => {
  const app = withProjects();
  app._metas = [{ id: 1, name: 'Alpha', color: '#16a34a' }, { id: 2, name: 'Beta' }];
  const stencil = createStencil(app);

  const p = stencil.getProjectByName('alpha');
  assert.equal(p.color, '#16a34a');
  p.color = '#abc';                       // normalises in DrawingApp; facade passes the trimmed value
  assert.deepEqual(lastCall(app, 'setProjectColor'), ['setProjectColor', 1, '#abc']);
  p.color = '';                           // clear is allowed
  assert.deepEqual(lastCall(app, 'setProjectColor'), ['setProjectColor', 1, '']);
  assert.throws(() => { p.color = 'zzz'; }, /Invalid project color/);
});

test('project.blank/blankColor: read-only blank flag, colour routes to setProjectBlankColor', () => {
  const app = withProjects();
  app._metas = [
    { id: 1, name: 'Alpha', blank: true, blankColor: '#00aaff' },
    { id: 2, name: 'Beta' },   // ordinary image project (not blank)
  ];
  const stencil = createStencil(app);

  const blankP = stencil.getProjectByName('alpha');
  assert.equal(blankP.blank, true);
  assert.equal(blankP.blankColor, '#00aaff');
  blankP.blankColor = '#ff0000';          // valid hex → routes to setProjectBlankColor
  assert.deepEqual(lastCall(app, 'setProjectBlankColor'), ['setProjectBlankColor', 1, '#ff0000']);
  assert.throws(() => { blankP.blankColor = 'zzz'; }, /Invalid blank color/);

  // A non-blank project: blank=false, blankColor=null, and setting throws (nothing to recolour).
  const imgP = stencil.getProjectByName('beta');
  assert.equal(imgP.blank, false);
  assert.equal(imgP.blankColor, null);
  assert.throws(() => { imgP.blankColor = '#ffffff'; }, /not a blank image/);
});

test('project.renew/open/close route to app and return the project for chaining', () => {
  const app = withProjects();
  const stencil = createStencil(app);
  const p = stencil.getProjectByName('beta');

  assert.equal(p.renew(), p);
  assert.deepEqual(lastCall(app, 'renewProject'), ['renewProject', 2]);
  assert.equal(p.open(), p);
  assert.deepEqual(lastCall(app, 'switchToProject'), ['switchToProject', 2]);
  p.close({ fully: true });
  assert.deepEqual(lastCall(app, 'closeProject'), ['closeProject', 2, { fully: true }]);
});

test('project expiration facade: expiresAt/refreshPeriod/autoRefresh/keepForever route to app', () => {
  const app = withProjects();
  const stencil = createStencil(app);
  const p = stencil.getProjectByName('beta');

  const future = Date.now() + 10 * 24 * 60 * 60 * 1000;
  p.expiresAt = future;
  assert.deepEqual(lastCall(app, 'setProjectExpiration'), ['setProjectExpiration', 2, { expiresAt: future }]);
  assert.throws(() => { p.expiresAt = Date.now() - 1000; }, /past/);
  assert.throws(() => { p.expiresAt = 'not-a-date'; }, /Invalid expiration/);

  p.refreshPeriod = 'month';
  assert.deepEqual(lastCall(app, 'setProjectExpiration'), ['setProjectExpiration', 2, { refreshPeriod: 'month' }]);
  assert.equal(p.refreshPeriod, 'month');
  assert.throws(() => { p.refreshPeriod = 'decade'; }, /Invalid refresh period/);

  p.autoRefresh = false;
  assert.deepEqual(lastCall(app, 'setProjectExpiration'), ['setProjectExpiration', 2, { autoRefresh: false }]);
  assert.equal(p.autoRefresh, false);

  assert.equal(p.keepForever(), p);
  assert.deepEqual(lastCall(app, 'setProjectExpiration'), ['setProjectExpiration', 2, { expiresAt: 0 }]);
});

test('project.expire(spec) parses a duration → setProjectExpiration; expirationDate get/set + top-level expire', () => {
  const app = withProjects();
  const stencil = createStencil(app);
  const p = stencil.getProjectByName('beta');
  const DAY = 24 * 60 * 60 * 1000;

  // No-arg → the formats help string (mutates nothing).
  const help = p.expire();
  assert.equal(typeof help, 'string');
  assert.match(help, /fortnight/);

  // A free-form duration resolves to now + span and routes through the shared setter.
  const before = Date.now();
  assert.equal(p.expire('months 3'), p); // chainable
  const call = lastCall(app, 'setProjectExpiration');
  assert.equal(call[1], 2);
  assert.ok(call[2].expiresAt >= before + 90 * DAY && call[2].expiresAt <= Date.now() + 90 * DAY, 'expiry ~90d out');

  // 'off' keeps it forever; an invalid spec throws.
  p.expire('off');
  assert.deepEqual(lastCall(app, 'setProjectExpiration'), ['setProjectExpiration', 2, { expiresAt: 0 }]);
  assert.throws(() => p.expire('banana'), /Invalid duration/);

  // expirationDate setter accepts a Date and routes exactly like expiresAt.
  const future = Date.now() + 5 * DAY;
  p.expirationDate = new Date(future);
  assert.deepEqual(lastCall(app, 'setProjectExpiration'), ['setProjectExpiration', 2, { expiresAt: future }]);
  assert.throws(() => { p.expirationDate = new Date(Date.now() - 1000); }, /past/);

  // Top-level stencil.expire(): no arg prints formats regardless of an active project.
  assert.match(stencil.expire(), /fortnight/);
});

test('incognito project: synthetic name + mutating links/name throw', () => {
  const app = makeApp({ storage: { ...makeApp().storage, incognito: true } });
  const stencil = createStencil(app);

  const inc = stencil.current;   // active id null + incognito storage → incognito project
  assert.equal(inc.incognito, true);
  assert.equal(inc.name, 'Incognito (unsaved)');
  assert.throws(() => { inc.name = 'x'; }, /Cannot rename an incognito/);
  assert.throws(() => { inc.source = 'http://x'; }, /Cannot set links on an incognito/);
});

// ── Zoom / crop ─────────────────────────────────────────────────────────────────
test('zoom: relative step recentres; passing a point keeps it fixed; zoomLevel get/set', () => {
  const app = makeApp({ scale: 1 });
  const stencil = createStencil(app);

  stencil.zoom(0.25);
  assert.deepEqual(lastCall(app, 'zoomAroundCenter'), ['zoomAroundCenter', 1.25]);
  stencil.zoom(0.5, { x: 10, y: 20 });
  assert.deepEqual(lastCall(app, 'zoomToImagePoint'), ['zoomToImagePoint', 1.5, 10, 20]);

  assert.equal(stencil.zoomLevel, 100);
  stencil.zoomLevel = 150;
  assert.deepEqual(lastCall(app, 'zoomAroundCenter'), ['zoomAroundCenter', 1.5]);
});

test('crop throws without an image, and otherwise commits a rect via applyCrop', () => {
  const app = makeApp();
  const stencil = createStencil(app);
  assert.throws(() => stencil.crop({ x1: 10 }), /No image loaded/);

  // Loaded image: provide the geometry crop() reads, assert applyCrop gets a numeric rect.
  const app2 = makeApp({
    originalImage: {},
    cropRect: { x: 0, y: 0, width: 100, height: 100 },
    effectiveOriginalDims: () => ({ w: 200, h: 200 }),
    getPageDimensions: () => ({ width: 21, height: 29.7 }),
    canvas: { width: 200, height: 200 },
    defaultCropRect: () => ({ x: 0, y: 0, width: 200, height: 200 }),
    applyCrop: function (rect, opts) { this.calls.push(['applyCrop', rect, opts]); },
  });
  const stencil2 = createStencil(app2);
  assert.equal(stencil2.crop({ x1: 10, x2: 90 }), stencil2);
  const [, rect, opts] = lastCall(app2, 'applyCrop');
  for (const k of ['x', 'y', 'width', 'height']) assert.equal(typeof rect[k], 'number');
  assert.deepEqual(opts, { recalc: true });
});

test('crop derives the missing axis from the page proportion when only one axis is given', () => {
  const makeCropApp = () => makeApp({
    originalImage: {},
    cropRect: { x: 5, y: 7, width: 100, height: 100 },
    effectiveOriginalDims: () => ({ w: 2000, h: 2000 }),
    getPageDimensions: () => ({ width: 21, height: 29.7 }),   // A4 proportions
    canvas: { width: 200, height: 200 },
    defaultCropRect: () => ({ x: 0, y: 0, width: 200, height: 200 }),
    applyCrop: function (rect, o) { this.calls.push(['applyCrop', rect, o]); },
  });
  const near = (a, b) => Math.abs(a - b) < 0.5;

  // Only the x axis is given → height is derived from width. Portrait (album false):
  // height = width × (29.7 / 21). The unspecified y keeps the current top edge (7).
  // (Absolute 'px' tokens — a bare number would be a delta from the current edge.)
  let app = makeCropApp();
  createStencil(app).crop({ x1: '10px', x2: '90px' });   // width 80
  let [, rect] = lastCall(app, 'applyCrop');
  assert.ok(near(rect.width, 80) && near(rect.height, 80 * 29.7 / 21), `portrait x→y ${JSON.stringify(rect)}`);
  assert.equal(rect.y, 7);

  // album true (landscape) inverts the relation: height = width × (21 / 29.7).
  app = makeCropApp();
  createStencil(app).crop({ x1: '10px', x2: '90px', album: true });
  [, rect] = lastCall(app, 'applyCrop');
  assert.ok(near(rect.height, 80 * 21 / 29.7), `landscape x→y ${JSON.stringify(rect)}`);

  // Only the y axis (and only one edge of it) → width is derived, x keeps its start.
  app = makeCropApp();
  createStencil(app).crop({ y2: '207px' });      // height = 207 - currentTop(7) = 200
  [, rect] = lastCall(app, 'applyCrop');
  assert.ok(near(rect.height, 200) && near(rect.width, 200 * 21 / 29.7), `portrait y→x ${JSON.stringify(rect)}`);
  assert.equal(rect.x, 5);

  // Both axes given → free-form, no proportion adjustment.
  app = makeCropApp();
  createStencil(app).crop({ x1: '0px', x2: '100px', y1: '0px', y2: '40px' });
  [, rect] = lastCall(app, 'applyCrop');
  assert.ok(near(rect.width, 100) && near(rect.height, 40), `free-form ${JSON.stringify(rect)}`);
});

test('crop({ scale }) scales the rect about its centre via applyCrop; rejects non-positive', () => {
  const makeScaleApp = () => makeApp({
    originalImage: {},
    cropRect: { x: 60, y: 60, width: 80, height: 80 },   // centre (100,100) in a 200x200 image
    effectiveOriginalDims: () => ({ w: 200, h: 200 }),
    applyCrop: function (rect, o) { this.calls.push(['applyCrop', rect, o]); },
  });

  // Grow 1.5×: 80 → 120, centre held at (100,100), committed with recalc (chainable).
  let app = makeScaleApp();
  const stencil = createStencil(app);
  assert.equal(stencil.crop({ scale: 1.5 }), stencil);
  let [, rect, opts] = lastCall(app, 'applyCrop');
  assert.ok(Math.abs(rect.width - 120) < 1e-6 && Math.abs(rect.height - 120) < 1e-6, JSON.stringify(rect));
  assert.ok(Math.abs(rect.x + rect.width / 2 - 100) < 1e-6 && Math.abs(rect.y + rect.height / 2 - 100) < 1e-6);
  assert.deepEqual(opts, { recalc: true });

  // Shrink 0.5×: 80 → 40.
  app = makeScaleApp();
  createStencil(app).crop({ scale: 0.5 });
  [, rect] = lastCall(app, 'applyCrop');
  assert.ok(Math.abs(rect.width - 40) < 1e-6, JSON.stringify(rect));

  // Non-positive / non-numeric scale throws BEFORE any applyCrop commit.
  for (const bad of [0, -2, 'not-a-number']) {
    const a = makeScaleApp();
    assert.throws(() => createStencil(a).crop({ scale: bad }), /crop scale must be a positive number/);
    assert.equal(called(a, 'applyCrop').length, 0);
  }
});

// ── crop({ aspect }) — port of core/tests/cropSpec.test.cpp's aspect cases ──────
const makeAspectApp = (cropRect) => makeApp({
  originalImage: {},
  cropRect,
  effectiveOriginalDims: () => ({ w: 4000, h: 4000 }),
  getPageDimensions: () => ({ width: 21, height: 29.7 }),
  canvas: { width: 200, height: 200 },
  defaultCropRect: () => ({ x: 0, y: 0, width: 4000, height: 4000 }),
  applyCrop: function (rect, o) { this.calls.push(['applyCrop', rect, o]); },
});
const eq = (a, b) => Math.abs(a - b) < 1e-9;

test('crop aspect shrinks the width about the centre', () => {
  // Edge rect 200x100 is too wide for 1:1 → width shrinks to 100, centred at x=100.
  const app = makeAspectApp({ x: 0, y: 0, width: 500, height: 500 });
  createStencil(app).crop({ x1: '0px', x2: '200px', y1: '0px', y2: '100px', aspect: '1:1' });
  const [, r] = lastCall(app, 'applyCrop');
  assert.ok(eq(r.x, 50) && eq(r.y, 0) && eq(r.width, 100) && eq(r.height, 100), JSON.stringify(r));
});

test('crop aspect shrinks the height about the centre', () => {
  // The same 200x100 rect is too tall for 4:1 → height shrinks to 50, centred at y=50.
  const app = makeAspectApp({ x: 0, y: 0, width: 500, height: 500 });
  createStencil(app).crop({ x1: '0px', x2: '200px', y1: '0px', y2: '100px', aspect: '4:1' });
  const [, r] = lastCall(app, 'applyCrop');
  assert.ok(eq(r.x, 0) && eq(r.y, 25) && eq(r.width, 200) && eq(r.height, 50), JSON.stringify(r));
});

test('crop aspect that already fits exactly is a no-op', () => {
  const app = makeAspectApp({ x: 0, y: 0, width: 500, height: 500 });
  createStencil(app).crop({ x1: '10px', x2: '210px', y1: '20px', y2: '120px', aspect: '2:1' });
  const [, r] = lastCall(app, 'applyCrop');
  assert.ok(eq(r.x, 10) && eq(r.y, 20) && eq(r.width, 200) && eq(r.height, 100), JSON.stringify(r));
});

test('crop with only aspect applies to the current full rect', () => {
  // No edges → the rect stays the current crop (headless: the full image), then fits 1:1.
  let app = makeAspectApp({ x: 0, y: 0, width: 640, height: 480 });
  createStencil(app).crop({ aspect: '1:1' });
  let [, r] = lastCall(app, 'applyCrop');
  assert.ok(eq(r.x, 80) && eq(r.y, 0) && eq(r.width, 480) && eq(r.height, 480), JSON.stringify(r));

  // Already at the ratio → untouched.
  app = makeAspectApp({ x: 0, y: 0, width: 640, height: 480 });
  createStencil(app).crop({ aspect: '4:3' });
  [, r] = lastCall(app, 'applyCrop');
  assert.ok(eq(r.x, 0) && eq(r.width, 640) && eq(r.height, 480), JSON.stringify(r));
});

test('crop aspect keeps fractional centres exactly (no early rounding)', () => {
  // 101x100 rect, 1:1 → width 100, so x moves by half a pixel: 0.5.
  const app = makeAspectApp({ x: 0, y: 0, width: 500, height: 500 });
  createStencil(app).crop({ x1: '0px', x2: '101px', y1: '0px', y2: '100px', aspect: '1:1' });
  const [, r] = lastCall(app, 'applyCrop');
  assert.ok(eq(r.x, 0.5) && eq(r.width, 100) && eq(r.height, 100), JSON.stringify(r));
});

test('crop aspect degenerate result clamps to 1px, centre kept', () => {
  const app = makeAspectApp({ x: 0, y: 0, width: 500, height: 500 });
  createStencil(app).crop({ x1: '0px', x2: '100px', y1: '0px', y2: '100px', aspect: '1000:1' });
  const [, r] = lastCall(app, 'applyCrop');
  assert.ok(eq(r.width, 100), JSON.stringify(r));    // never grows past the resolved rect
  assert.ok(eq(r.height, 1), JSON.stringify(r));     // 0.1px floored to 1px
  assert.ok(eq(r.y, 49.5) && eq(r.x, 0), JSON.stringify(r));  // centre preserved
});

test('crop aspect invalid strings throw before any applyCrop commit', () => {
  for (const bad of ['0:3', '4:0', '-4:3', '4:-3', '4', '4:', ':3', '4:3:2',
                     'a:b', '4.5:3', '1e2:3', '']) {
    const app = makeAspectApp({ x: 0, y: 0, width: 640, height: 480 });
    assert.throws(() => createStencil(app).crop({ aspect: bad }),
                  /crop aspect must be "W:H" with positive integers/, JSON.stringify(bad));
    assert.equal(called(app, 'applyCrop').length, 0);
  }
});

// ── Incognito toggle guard ────────────────────────────────────────────────────────
test('incognito setter only enables on a blank editor', () => {
  const app = makeApp();
  const stencil = createStencil(app);

  app.image = { width: 1, height: 1 };
  assert.throws(() => { stencil.incognito = true; }, /blank editor/);

  app.image = null;
  stencil.incognito = true;
  assert.equal(app.storage.incognito, true);
  assert.equal(called(app, 'updateIncognitoUI').length, 1);
});

// ── Shortcuts (real hotkeys singleton) ─────────────────────────────────────────────
test('changeShortcut rebinds, rejects unknown refs, and rejects conflicts', () => {
  const app = makeApp();
  const stencil = createStencil(app);

  const entries = hotkeys.entries();
  assert.ok(entries.length >= 2, 'need at least two shortcuts to test conflicts');
  const [id0, combo0] = entries[0];
  const [, combo1] = entries[1];
  const FREE = 'ctrl+shift+f13';

  try {
    assert.throws(() => stencil.changeShortcut('no-such-action', FREE), /No shortcut matches/);
    assert.throws(() => stencil.changeShortcut(id0, combo1), /already bound/);

    assert.equal(stencil.changeShortcut(id0, FREE), stencil);
    assert.equal(stencil.shortcuts[id0], FREE);
    // oldRef may be the current combo string too.
    stencil.changeShortcut(FREE, combo0);
    assert.equal(stencil.shortcuts[id0], combo0);
  } finally {
    hotkeys.set(id0, combo0);   // restore the singleton for other test files
  }
});

// ── Full settings sweep ───────────────────────────────────────────────────────────
test('every documented flattened setting routes to its app setter with the expected arg', () => {
  const app = makeApp();
  const stencil = createStencil(app);

  // [facade key, app setter, value to assign, expected setter arg]. Hex values dodge the
  // canvas-less toHexColor (named colors would pass through unchanged anyway).
  const DIRECT = [
    ['unit', 'setUnit', 'in', 'in'],
    ['lineColor', 'setColor', '#abcdef', '#abcdef'],
    ['thickness', 'setThickness', 3, 3],
    ['pointSize', 'setPointSize', 9, 9],
    ['pointSize', 'setPointSize', 9, 9],
    ['lineStyle', 'setLineStyle', 'dashed', 'dashed'],
    ['pointStyle', 'setShowPoints', true, true],
    ['showPoints', 'setShowPoints', true, true],
    ['showLines', 'setShowLines', false, false],
    ['filter', 'setImageFilter', 'sepia', 'sepia'],
    ['filterColor', 'setFilterColor', '#7c3aed', '#7c3aed'],
    ['pageSize', 'setPageSize', 'a3', 'a3'],
    ['pageWidth', 'setCustomPageWidth', 30, 30],
    ['pageHeight', 'setCustomPageHeight', 40, 40],
    ['darkTheme', 'setTheme', true, 'dark'],
    ['allowFormulas', 'setAllowFormulas', true, true],
  ];
  for (const [key, setter, value, expected] of DIRECT) {
    stencil[key] = value;
    assert.deepEqual(lastCall(app, setter), [setter, expected], `${key} → ${setter}`);
  }

  // Special routing: drawMode normalizes, formulas pick an axis, visual colors pick a channel.
  stencil.drawMode = 'rect';
  assert.deepEqual(lastCall(app, 'setDrawMode'), ['setDrawMode', 'rect']);
  stencil.holdDrawDelay = 750;
  assert.deepEqual(lastCall(app, 'setHoldDrawDelay'), ['setHoldDrawDelay', 750]);
  stencil.formulaX = 'x*2';
  assert.deepEqual(lastCall(app, 'setFormula'), ['setFormula', 'x', 'x*2']);
  stencil.formulaY = 'y+1';
  assert.deepEqual(lastCall(app, 'setFormula'), ['setFormula', 'y', 'y+1']);
  for (const [key, channel] of [['fillColor', 'fill'], ['selectionGlow', 'selGlow'], ['hoverRing', 'hoverRing'], ['focusRing', 'focusRing']]) {
    stencil[key] = '#123456';
    assert.deepEqual(lastCall(app, 'setVisualColor'), ['setVisualColor', channel, '#123456'], `${key} → setVisualColor`);
  }
});

// mainTheme: preset keys persist+sync via setAccent; a hex applies a temp page-local accent
// via setCustomAccent; the getter prefers an active custom hex; junk throws.
test('mainTheme accepts preset keys and custom hex colours', () => {
  const app = makeApp();
  const stencil = createStencil(app);

  assert.equal(stencil.mainTheme, 'violet');         // preset key from app.accent

  stencil.mainTheme = 'GREEN';                        // case-insensitive preset
  assert.deepEqual(lastCall(app, 'setAccent'), ['setAccent', 'green']);
  assert.equal(stencil.mainTheme, 'green');

  stencil.mainTheme = '#FF5623';                      // custom hex → normalized, page-local
  assert.deepEqual(lastCall(app, 'setCustomAccent'), ['setCustomAccent', '#ff5623']);
  assert.equal(stencil.mainTheme, '#ff5623');         // getter prefers the custom hex

  stencil.mainTheme = 'f50';                          // short hex, no '#'
  assert.deepEqual(lastCall(app, 'setCustomAccent'), ['setCustomAccent', '#ff5500']);

  assert.throws(() => { stencil.mainTheme = 'notacolour'; }, /Unknown theme/);
});

// ── Tooltip / imageSize / layout / coordinate conversion / viewport / fullscreen ──
test('tooltip sections, imageSize, and layout get/set route to the app', () => {
  const app = makeApp({ image: { width: 800, height: 600 } });
  const stencil = createStencil(app);

  assert.deepEqual(stencil.imageSize, { width: 800, height: 600 });

  stencil.tooltip.enabled = false;
  assert.deepEqual(lastCall(app, 'setTooltipOption'), ['setTooltipOption', 'enabled', false]);
  assert.equal(stencil.tooltip.page, true);   // reads app.tooltipShowPage

  stencil.layout = { foo: 1 };
  assert.deepEqual(lastCall(app, 'applyPastedLayout'), ['applyPastedLayout', { foo: 1 }]);
});

test('setLines installs lines silently — no paste prompt, no toast, optional undo', () => {
  // `stencil.layout = …` is the clipboard-paste path (it can raise "Replace layout?" and
  // always toasts). setLines is the programmatic one: same install, none of the UI.
  const app = makeApp({ image: { width: 800, height: 600 } });
  const stencil = createStencil(app);
  const lines = [{ points: [{ x: 1, y: 2 }], color: '#ff0000' }];

  stencil.setLines(lines, { history: false });
  // The current image dims ride along so validateLayout sees a matching size.
  assert.deepEqual(lastCall(app, 'installLayout'),
    ['installLayout', { imageWidth: 800, imageHeight: 600, lines }, { history: false }]);
  // It never reaches the paste path, so nothing can prompt or toast.
  assert.equal(app.calls.filter((c) => c[0] === 'applyPastedLayout').length, 0);

  // Defaults keep the change undoable; a non-array is treated as "no lines".
  stencil.setLines(lines);
  assert.deepEqual(lastCall(app, 'installLayout')[2], {});
  stencil.setLines(null);
  assert.deepEqual(lastCall(app, 'installLayout')[1].lines, []);
  // Chainable like the rest of the facade.
  assert.equal(stencil.setLines([]), stencil);
});

test('px2Page / page2Px convert via the app mapping', () => {
  const app = makeApp();
  const stencil = createStencil(app);

  assert.deepEqual(stencil.px2Page({ x: 100, y: 200 }), { x: 10, y: 20 });
  // page2Px: (cm / pageDim) * canvasPx → (5/20)*200=50 , (15/30)*300=150
  assert.deepEqual(stencil.page2Px({ x: 5, y: 15 }), { x: 50, y: 150 });
});

test('move pans the viewport; fullscreen get/set toggles via the app', () => {
  const app = makeApp();
  const stencil = createStencil(app);

  assert.equal(stencil.move({ x: 10, y: -4 }), stencil);
  assert.equal(viewport.scrollLeft, 10);
  assert.equal(viewport.scrollTop, -4);

  assert.equal(stencil.fullscreen, false);
  stencil.fullscreen = true;
  assert.equal(called(app, 'toggleFullscreen').length, 1);
  assert.equal(stencil.fullscreen, true);
  stencil.fullscreen = true;                       // already on → no extra toggle
  assert.equal(called(app, 'toggleFullscreen').length, 1);
});

test('blank() creates a solid image via the app and resolves to the facade', async () => {
  const app = makeApp();
  const stencil = createStencil(app);

  const ret = await stencil.blank('red', { size: { width: 800, height: 600 } });
  assert.equal(ret, stencil);
  assert.deepEqual(lastCall(app, 'createBlankImage'), ['createBlankImage', { color: 'red', width: 800, height: 600 }]);
  assert.deepEqual(stencil.imageSize, { width: 800, height: 600 });
});

// ── Project collections ───────────────────────────────────────────────────────────
test('project collections: opened / archived / getProjects honour the open set', () => {
  const app = withProjects();   // active id 1; metas Alpha(1), Beta(2)
  const stencil = createStencil(app);

  assert.deepEqual(stencil.openedProjects.map((p) => p.id), [1]);    // only the active id is open
  assert.deepEqual(stencil.archivedProjects.map((p) => p.id), [2]);  // saved but not open
  assert.deepEqual(stencil.incognitoProjects, []);                   // no incognito editor here
  assert.deepEqual(stencil.getProjects().map((p) => p.id), [1]);     // default = currently open
  assert.deepEqual(stencil.getProjects({ archived: true }).map((p) => p.id), [1, 2]);
});

test('active project: source/resource/imageName set live; size/isOpened/layout read through', () => {
  const app = withProjects();
  const stencil = createStencil(app);
  app.image = { width: 640, height: 480 };

  const p = stencil.current;   // id 1, the active project
  assert.equal(p.isOpened, true);
  assert.deepEqual(p.size, { image: { width: 640, height: 480 } });
  assert.equal(p.imageName, 'pic.png');

  p.source = 'http://x/i.png';
  assert.equal(app.imageSource, 'http://x/i.png');
  p.resource = 'http://x/page';
  assert.equal(app.imageResource, 'http://x/page');
  p.imageName = 'renamed.png';
  assert.equal(app.imageBaseName, 'renamed.png');
  assert.ok(called(app, 'save').length >= 3);   // each live edit flushes storage

  assert.deepEqual(p.layout, {});   // store().get(1).payload.layout
});

// ── Line individual setters + point setters/remove ──────────────────────────────────
test('individual line setters commit each change', () => {
  const app = makeApp({ lines: [{ color: '#000000', thickness: 1, pointSize: 1, style: 'solid', fillColor: 'transparent', points: [{ x: 0, y: 0 }] }] });
  const stencil = createStencil(app);
  const line = stencil.lines[0];

  line.color = '#ABC';      assert.equal(app.lines[0].color, '#aabbcc');
  line.thickness = 4;       assert.equal(app.lines[0].thickness, 4);
  line.pointSize = 8;      assert.equal(app.lines[0].pointSize, 8);
  line.style = 'dotted';    assert.equal(app.lines[0].style, 'dotted');
  line.fillColor = '#3399ff'; assert.equal(app.lines[0].fillColor, '#3399ff');
  line.fillColor = null;    assert.equal(app.lines[0].fillColor, 'transparent');   // null → transparent
  assert.equal(called(app, 'saveHistory').length, 6);   // one commit per setter
});

test('point x/y setters write absolute coords; pt.remove drops the point (and empties → drops the line)', () => {
  const app = makeApp({ lines: [{ points: [{ x: 1, y: 2 }] }] });
  const stencil = createStencil(app);

  const pt = stencil.lines[0].points[0];
  pt.x = 50; pt.y = 60;
  assert.deepEqual(app.lines[0].points[0], { x: 50, y: 60 });
  assert.deepEqual(lastCall(app, 'setPointCoord'), ['setPointCoord', 0, 0, 'y', 60]);

  // Removing the only point empties the line → the line is dropped, and remove() returns the facade.
  assert.equal(pt.remove(), stencil);
  assert.equal(app.lines.length, 0);
});

// ── Prototype-pollution lock-in ─────────────────────────────────────────────────────
// Security regression guards: the facade must never let a caller-supplied object walk into
// Object.prototype. `apply()` iterates a FIXED key allowlist (never Object.keys(opts)), and
// the layout setter routes to applyPastedLayout → validateLayout, whose sanitizeLines rebuilds
// each line from a whitelist onto a fresh plain object (dropping __proto__/constructor/…).
test('apply() ignores a polluting __proto__ payload and never touches Object.prototype', () => {
  const app = makeApp();
  const stencil = createStencil(app);

  // Object-literal form: `__proto__` is the object's prototype, not an own key — apply()
  // still only reads its allowlisted keys, so nothing reaches Object.prototype.
  stencil.apply({ __proto__: { polluted: 1 }, thickness: 3 });
  assert.equal(({}).polluted, undefined);
  assert.equal(Object.prototype.polluted, undefined);
  assert.deepEqual(lastCall(app, 'setThickness'), ['setThickness', 3]);   // legit key still routed

  // JSON-parsed form: here `__proto__` IS an own enumerable key. apply() never iterates it
  // (it walks a fixed allowlist), so it can't be re-assigned onto anything shared.
  const evil = JSON.parse('{"__proto__":{"polluted":2},"thickness":5}');
  stencil.apply(evil);
  assert.equal(({}).polluted, undefined);
  assert.equal(Object.prototype.polluted, undefined);
  assert.deepEqual(lastCall(app, 'setThickness'), ['setThickness', 5]);
});

test('layout setter routes to applyPastedLayout → validateLayout without polluting Object.prototype', () => {
  // Wire the mock's applyPastedLayout to the REAL validateLayout so the sanitize path
  // (which strips __proto__ and rebuilds lines onto fresh objects) is exercised end-to-end.
  const app = makeApp({
    image: { width: 10, height: 10 },
    canvas: { width: 10, height: 10 },
    lines: [],
    applyPastedLayout(data) {
      app.calls.push(['applyPastedLayout', data]);
      const v = validateLayout(data, { hasImage: true, imgW: 10, imgH: 10, hasExistingLines: false });
      app.lines = v.lines;
    },
  });
  const stencil = createStencil(app);

  // A JSON-parsed layout carrying an OWN "__proto__" key both at the top level and inside a
  // line entry — the dangerous shape that a naive deep-merge would splat onto Object.prototype.
  const evilLayout = JSON.parse(
    '{"__proto__":{"x":1},"lines":[{"points":[{"x":1,"y":2}],"color":"#ff0000","__proto__":{"x":2}}]}',
  );
  stencil.layout = evilLayout;

  assert.deepEqual(lastCall(app, 'applyPastedLayout')[1], evilLayout);   // routed through the facade
  assert.equal(({}).x, undefined);
  assert.equal(Object.prototype.x, undefined);

  // The sanitized line kept its real fields but carries no injected own key.
  assert.equal(app.lines.length, 1);
  assert.deepEqual(app.lines[0].points, [{ x: 1, y: 2 }]);
  assert.equal(app.lines[0].color, '#ff0000');
  assert.ok(!Object.prototype.hasOwnProperty.call(app.lines[0], '__proto__'));
});

// ── load(url, { incognito }) — llm-contract.md §10 openUrl, adopt-in-place ──────────
// The chat's incognito openUrl runs THROUGH this: the editor becomes a fresh incognito
// session and the picture lands in the SAME tab, so the conversation driving it (and
// every later action in the plan) still has the working image in front of it.
const stubFetch = (blob) => installFetchStub(blob instanceof Error ? blob : { blob }).restore;

test('load({ incognito }) adopts incognito in place and loads here — no new tab', async () => {
  const app = makeApp();
  app.adoptIncognitoHere = () => {
    app.calls.push(['adoptIncognitoHere']);
    app.storage.incognito = true;
    app.image = null;
  };
  app.loadImageFromFile = (file, opts) => {
    app.calls.push(['loadImageFromFile', file.name, opts]);
    app.image = { width: 4, height: 3 };
  };
  const stencil = createStencil(app);
  const restore = stubFetch(new Blob(['x'], { type: 'image/png' }));
  try {
    await stencil.load('https://pics.example/cat.png', { incognito: true });
  } finally { restore(); }

  const order = app.calls.map(([n]) => n).filter((n) => n === 'adoptIncognitoHere' || n === 'loadImageFromFile');
  assert.deepEqual(order, ['adoptIncognitoHere', 'loadImageFromFile']);
  assert.equal(app.storage.incognito, true);
  assert.deepEqual(app.image, { width: 4, height: 3 });
});

test('load() without incognito never resets the editor', async () => {
  const app = makeApp();
  app.adoptIncognitoHere = () => { app.calls.push(['adoptIncognitoHere']); };
  app.loadImageFromFile = () => { app.image = { width: 4, height: 3 }; };
  const stencil = createStencil(app);
  const restore = stubFetch(new Blob(['x'], { type: 'image/png' }));
  try {
    await stencil.load('https://pics.example/cat.png');
  } finally { restore(); }
  assert.equal(called(app, 'adoptIncognitoHere').length, 0);
  assert.equal(app.storage.incognito, false);
});

test('a failed fetch leaves the editor alone — the adoption never runs', async () => {
  const app = makeApp();
  app.image = { width: 9, height: 9 };
  app.adoptIncognitoHere = () => { app.calls.push(['adoptIncognitoHere']); };
  const stencil = createStencil(app);
  const restore = stubFetch(new Error('offline'));
  try {
    await assert.rejects(() => stencil.load('https://pics.example/cat.png', { incognito: true }), /offline/);
  } finally { restore(); }
  assert.equal(called(app, 'adoptIncognitoHere').length, 0);
  assert.equal(app.storage.incognito, false);
  assert.deepEqual(app.image, { width: 9, height: 9 });
});

test('chat.swapSides gets/sets the tab-session-only side and restamps both transcripts live', () => {
  const app = makeApp();
  const stencil = createStencil(app);
  assert.equal(stencil.chat.swapSides, false, 'default is normal, unswapped');
  stencil.chat.swapSides = true;
  assert.equal(stencil.chat.swapSides, true);
  assert.ok(chatTranscript.classes.has('chat-swapped'), 'the panel transcript restamped immediately');
  assert.ok(ctxAssistTranscript.classes.has('chat-swapped'), 'the ctx-assist transcript restamped too');
  stencil.chat.swapSides = false;
  assert.equal(stencil.chat.swapSides, false);
  assert.ok(!chatTranscript.classes.has('chat-swapped'));
  assert.ok(!ctxAssistTranscript.classes.has('chat-swapped'));
});
