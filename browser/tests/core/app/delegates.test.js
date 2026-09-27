// DrawingApp takes on only its view seam and its DOM-free mixin (core/app/delegates.js): a core
// function is imported by its callers, never forwarded through the app, and a collaborator's method
// is called on the collaborator. The seam lands as named, non-enumerable methods.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { DrawingApp } from '../../../js/core/drawingApp.js';
import { installDelegates, installMethods } from '../../../js/core/app/delegates.js';
import { EditingMethods } from '../../../js/core/app/editing.js';

const JS = path.join(path.dirname(fileURLToPath(import.meta.url)), '..', '..', '..', 'js');
const own = (cls) => Object.getOwnPropertyNames(cls.prototype).filter((n) => n !== 'constructor');

// The seam is what core/, llm/ and console/ reach the view through; ui/ imports these itself.
const VIEW_SEAM = ['hideSelectionPanels', 'applyLinesListHover', 'showSelectionPanel', 'syncDrawModeUI',
  'updateCoordStatus', 'applyUnitToUI', 'updateButtons', 'updateProjectTitle', 'updateInfo'];

// Names that used to be forwarded: each is its module's function, or its collaborator's method.
const RETIRED = [
  'nearCompareDivider', 'canvasCoords', 'openInLaunchPayload', 'currentLayoutPayload',
  'loadImageFromFile', 'applyExternalLaunch', 'startDrawingMode', 'setDrawMode', 'stopDrawingMode',
  'selectedIndices', 'updateMultiSelectStatus', 'selectLineFromList',
  'setListHoverLine', 'deselectEmptyArea', 'applySelectionChange', 'canvasClick',
  'tryCloseShapeAt', 'insertPointOnSegment', 'createRect', 'canvasMouseMove', 'canvasDblClick',
  'beginSegmentDrag', 'movePointTo', 'endPointDrag', 'endSegmentDrag', 'dragMove',
  'finishDragGesture', 'getPageDimensions', 'pixelToPageCoords', 'rotateSelectedLine',
  'flipSelectedLine', 'rotateSelectedLineQuarter', 'nudgeSelected', 'newEditor', 'openImageHere',
  'replaceProjectImage', 'createRemoteBlank', 'adoptIncognitoHere', 'publishIncognitoToServer',
  'promoteIncognitoToLocal', 'canToggleIncognito', 'reportIncognitoSession', 'createBlankImage',
  'activeIsBlank', 'setBlankColor', 'projectFileState', 'applyProjectFile',
  'applyProjectFileInPlace', 'chooseFileConflict', 'updateStencilSyncUI', 'setPointCoord',
  'removePoint', 'removeLine', 'removeSelectedLines', 'renderResultCanvas', 'setTheme',
  'setAccent', 'setCustomAccent', 'previewAccent', 'endAccentPreview', 'openRemoteProject',
  'switchToProject', 'openProjectInNewTab', 'openRemoteProjectInNewTab', 'clearAllProjects',
  'renewProject', 'setProjectExpiration', 'renameProject', 'setProjectColor', 'setProjectKeywords',
  'setProjectDescription', 'setProjectBlankColor', 'removeProject', 'moveProjectToServer',
  'copyProjectToServer', 'moveProjectToLocal', 'copyServerProjectToLocal',
  'copyServerProjectToIncognito', 'renderLinesList', 'applyFill', 'syncFsSelectionPanel',
  'syncDrawToggleUI',
];

test('the view seam lands on the prototype as named, non-enumerable methods, and nothing else is forwarded', () => {
  for (const name of VIEW_SEAM) {
    const d = Object.getOwnPropertyDescriptor(DrawingApp.prototype, name);
    assert.equal(typeof d?.value, 'function', name);
    assert.equal(d.enumerable, false, `${name} is not enumerable, like a class method`);
    assert.equal(d.value.name, name);
  }
  for (const name of RETIRED) assert.equal(name in DrawingApp.prototype, false, `${name} is no pass-through`);
  for (const name of own(EditingMethods)) assert.ok(!VIEW_SEAM.includes(name), `${name} is written out, not forwarded`);
  assert.ok(DrawingApp.prototype.undo === EditingMethods.prototype.undo, 'the mixin methods are installed as they are');
});

test('no module calls a retired pass-through on the app', () => {
  const calls = new RegExp(`\\b(?:app|this\\.app)\\??\\.(?:${RETIRED.join('|')})\\b\\??\\.?\\(`);
  const hits = [];
  const walk = (dir) => {
    for (const e of fs.readdirSync(dir, { withFileTypes: true })) {
      const p = path.join(dir, e.name);
      if (e.isDirectory()) { if (e.name !== 'wasm') walk(p); } else if (e.name.endsWith('.js')) {
        fs.readFileSync(p, 'utf8').split('\n').forEach((line, i) => {
          if (calls.test(line.replace(/\/\/.*$/, ''))) hits.push(`${path.relative(JS, p)}:${i + 1}`);
        });
      }
    }
  };
  walk(JS);
  assert.deepEqual(hits, []);
});

test('a forwarder hands the app first and returns what its target returns', () => {
  class Host {}
  const seen = [];
  installDelegates(Host.prototype, { probe: (app, ...a) => { seen.push([app, a]); return 'r'; } });
  const h = new Host();
  assert.equal(h.probe(1, 2), 'r');
  assert.deepEqual(seen, [[h, [1, 2]]]);
});

test('installMethods copies a class\'s methods, getters included, without its constructor', () => {
  class Mixin { get twice() { return this.v * 2; } bump() { this.v++; return this; } }
  class Host { v = 2; }
  installMethods(Host.prototype, Mixin);
  const h = new Host();
  assert.equal(h.bump().twice, 6);
  assert.ok(!Object.hasOwn(Host.prototype, 'constructor') || Host.prototype.constructor === Host);
});
