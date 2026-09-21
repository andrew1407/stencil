// Import-direction lint for browser/js. The layer order (ARCHITECTURE.md) is config/ + utils → core
// (no DOM) → eventBus → net → llm → console → ui → render; a layer may use everything to its left.
// Three text-only rules: js/core touches no `document`/`window`; js/core, js/llm and js/net import
// nothing from js/ui; and only js/console (with the boot shim installing it) may say `window.stencil`.
// Every site today's tree still crosses is a frozen allowance: a new file or a grown count fails.
import test from 'node:test';
import assert from 'node:assert';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const JS = path.join(path.dirname(fileURLToPath(import.meta.url)), '..', 'js');

const walk = (rel, out = []) => {
  for (const e of fs.readdirSync(path.join(JS, rel), { withFileTypes: true })) {
    const child = rel ? `${rel}/${e.name}` : e.name;
    if (e.isDirectory()) { if (e.name !== 'wasm') walk(child, out); }
    else if (e.name.endsWith('.js')) out.push(child);
  }
  return out.sort();
};

// Code only: comments and, unless `keepStrings`, string/template bodies are stripped, so a URL or a
// "window" in prose never counts; import specifiers are strings, so rule 2 keeps them.
const stripped = (rel, keepStrings = false) => {
  const src = fs.readFileSync(path.join(JS, rel), 'utf8');
  let out = '', quote = '', block = false, line = false;
  for (let i = 0; i < src.length; i++) {
    const c = src[i], n = src[i + 1];
    if (line) { if (c === '\n') { line = false; out += c; } continue; }
    if (block) { if (c === '*' && n === '/') { block = false; i++; } continue; }
    if (quote) {
      if (c === '\\') { if (keepStrings) out += c + n; i++; }
      else if (c === quote) { quote = ''; out += c; }
      else if (c === '\n' || keepStrings) out += c;
      continue;
    }
    if (c === '/' && n === '/') { line = true; continue; }
    if (c === '/' && n === '*') { block = true; i++; continue; }
    if (c === '/' && REGEX_MAY_START.test(out)) { i = regexEnd(src, i); continue; }
    if (c === '"' || c === "'" || c === '`') quote = c;
    out += c;
  }
  return out;
};

// A `/` after an operator, an opener or `return` opens a regex literal, not a division;
// its body (which may hold a quote, e.g. /"/g) is skipped up to the closing `/`.
const REGEX_MAY_START = /(?:[(,=:[!&|?{};+\-*%<>~^]|\b(?:return|typeof|case|in|of))\s*$/;
const regexEnd = (src, i) => {
  let cls = false;
  for (i++; i < src.length && src[i] !== '\n'; i++) {
    if (src[i] === '\\') i++;
    else if (cls) cls = src[i] !== ']';
    else if (src[i] === '[') cls = true;
    else if (src[i] === '/') break;
  }
  return i;
};

const count = (text, re) => (text.match(re) || []).length;
// A bare global, not a property (`app.window`) and not an object key (`{ window: … }`).
const DOM_GLOBAL = /(?<![\w$.])(?:window|document)\b(?!\s*:)/g;
const UI_IMPORT = /(?:from|import\s*\()\s*'(?:\.\.\/)+ui\//g;
const FACADE = /\b(?:window|globalThis)\.stencil\b/g;

// Enforce a frozen allowance: every measured site must be listed with a count no lower
// than today's. A shrunk count is reported so the entry can be lowered in the same commit.
const ratchet = (name, measured, allowance) => {
  const appeared = [], grew = [], shrank = [], gone = [];
  for (const [rel, n] of measured) {
    const cap = allowance[rel];
    if (cap === undefined) appeared.push(`'${rel}': ${n}`);
    else if (n > cap) grew.push(`${rel}: ${cap} → ${n}`);
    else if (n < cap) shrank.push(`'${rel}': ${n}`);
  }
  for (const rel of Object.keys(allowance)) if (!measured.has(rel)) gone.push(rel);
  if (shrank.length) console.log(`  note: ${name} — lower these allowances:\n    ${shrank.join('\n    ')}`);
  assert.deepStrictEqual(appeared, [], `${name}: new sites — move the call behind the layer's seam, not into the allowance`);
  assert.deepStrictEqual(grew, [], `${name}: a listed file crosses the line more than it did`);
  assert.deepStrictEqual(gone, [], `${name}: drop these entries — the file no longer crosses`);
};

const measure = (files, re, keepStrings = false) => {
  const m = new Map();
  for (const rel of files) { const n = count(stripped(rel, keepStrings), re); if (n) m.set(rel, n); }
  return m;
};

// Rule 1 — js/core is DOM-free: these files still reach for the DOM (the view paints, the storage
// adapter reads localStorage's window, the coordinators listen on window). Nothing may join them.
const CORE_DOM_ALLOWANCE = {
  'core/accents.js': 6, 'core/blankImage.js': 1, 'core/drawingApp.js': 16,
  'core/exportService.js': 3, 'core/extensionBridge.js': 2, 'core/hotkeys.js': 4,
  'core/imageFilterCanvas.js': 1, 'core/imageModel.js': 2, 'core/imageSettle.js': 1,
  'core/inputController.js': 7, 'core/launchController.js': 4, 'core/layoutInstall.js': 1,
  'core/lineSelection.js': 1, 'core/pointerController.js': 5, 'core/projectFileIO.js': 2,
  'core/projectFilePicker.js': 6, 'core/projectMeta.js': 1, 'core/projectServerTransfer.js': 1, 'core/projectTransferController.js': 3,
  'core/quotaWriter.js': 1, 'core/stencilSync.js': 2, 'core/storage.js': 1,
  'core/tabsCoordinator.js': 3, 'core/videoFrame.js': 1,
  'core/viewportSync.js': 6, 'core/zoomAnimation.js': 2, 'core/zoomPan.js': 1,
};

test('js/core touches no document/window beyond the frozen allowance', () => {
  ratchet('core DOM', measure(walk('core'), DOM_GLOBAL), CORE_DOM_ALLOWANCE);
});

// Rule 2 — core, llm and net import nothing from ui: core still pulls the paint helpers it drives, each
// leaving as the controller behind it gains a view seam. llm and net are clean.
const UI_IMPORT_ALLOWANCE = {
  'core/drawingApp.js': 10, 'core/hotkeys.js': 1, 'core/imageSettle.js': 1,
  'core/launchController.js': 1, 'core/layoutInstall.js': 1, 'core/projectFilePicker.js': 1,
  'core/remoteSyncController.js': 1, 'core/settingsController.js': 3, 'core/settingsRegistry.js': 1,
  'core/shapeBuilder.js': 1, 'core/storage.js': 4, 'core/strokeFx.js': 2,
};

test('js/core, js/llm and js/net import nothing from js/ui beyond the frozen allowance', () => {
  const files = [...walk('core'), ...walk('llm'), ...walk('net')];
  ratchet('ui import', measure(files, UI_IMPORT, true), UI_IMPORT_ALLOWANCE);
});

// Rule 3 — only the console layer speaks `window.stencil`: index.js installs it by defineProperty and
// never reads it back; the sites below should be handed the facade instead.
const FACADE_ALLOWANCE = { 'llm/adapters/media.js': 1, 'llm/chatSession.js': 1, 'ui/chatCards.js': 1 };

test('window.stencil appears outside js/console only in the frozen allowance', () => {
  const files = walk('').filter((rel) => !rel.startsWith('console/'));
  ratchet('facade', measure(files, FACADE), FACADE_ALLOWANCE);
});

// Rule 4 — a region owns its own subtree: `ui/` resolves nodes through StencilElement's scoped
// `this.$(id)`, not the document. Every file that still reaches globally is frozen here, so a
// new one, or an old one reaching further, fails. (`ui/base.js` is the one `$` definition.)
const GLOBAL_ID = /(?<![\w$.])(?:document\.getElementById|\$)\(/g;

const UI_ID_ALLOWANCE = {
  'ui/base.js': 1, 'ui/bindings/arrowPan.js': 1, 'ui/bindings/blankColorButton.js': 2, 'ui/bindings/canvasPointer.js': 1,
  'ui/bindings/dropPaste.js': 1, 'ui/bindings/formula.js': 1, 'ui/bindings/hotkeyActions.js': 9,
  'ui/bindings/index.js': 1, 'ui/bindings/pageAndDisplay.js': 8, 'ui/bindings/projectColorButton.js': 3,
  'ui/bindings/projectNameField.js': 6, 'ui/bindings/scrollPersist.js': 1, 'ui/bindings/selectionPanel.js': 10,
  'ui/bindings/smoothZoom.js': 1, 'ui/bindings/styleControls.js': 14, 'ui/bindings/theme.js': 1,
  'ui/bindings/toolbarButtons.js': 20, 'ui/bindings/zoom.js': 2, 'ui/chatCards.js': 1, 'ui/chatPanel.js': 22,
  'ui/confirmModal.js': 9, 'ui/connectModal.js': 19, 'ui/contextMenu.js': 3, 'ui/controlState.js': 16,
  'ui/cropModal.js': 11, 'ui/ctxActions.js': 22, 'ui/ctxAssistant.js': 6, 'ui/ctxAssistantChat.js': 15,
  'ui/ctxScript.js': 3, 'ui/ctxScriptEditor.js': 7, 'ui/ctxState.js': 22, 'ui/ctxStyleActions.js': 21,
  'ui/drawToggleUI.js': 2, 'ui/expirationModal.js': 2, 'ui/fullscreenClones.js': 2, 'ui/fullscreenLayer.js': 9,
  'ui/fullscreenPanels.js': 2, 'ui/imageMissingBanner.js': 3, 'ui/infoModal.js': 5, 'ui/keywordChips.js': 5,
  'ui/layoutControls.js': 1, 'ui/linesList.js': 2, 'ui/linksModal.js': 10, 'ui/llmSettingsModal.js': 21,
  'ui/mainContent.js': 9, 'ui/modalShell.js': 1, 'ui/openImage/modal.js': 5, 'ui/openInModal.js': 12,
  'ui/projectMetaModal.js': 7, 'ui/projectTitle.js': 10, 'ui/projects/batchActions.js': 1, 'ui/projects/selection.js': 8,
  'ui/projectsModal.js': 13, 'ui/scriptEditor.js': 11, 'ui/scriptModal.js': 10, 'ui/selectionPanel.js': 17,
  'ui/serverLayoutPaint.js': 3, 'ui/settingMirrors.js': 1, 'ui/settingsModal.js': 5, 'ui/toolbar.js': 7,
  'ui/unitDisplay.js': 5, 'ui/visualsModal.js': 18, 'ui/visualsVoiceRow.js': 1,
};

test('js/ui reaches the document by id only in the frozen allowance', () => {
  ratchet('ui id lookup', measure(walk('ui'), GLOBAL_ID), UI_ID_ALLOWANCE);
});
