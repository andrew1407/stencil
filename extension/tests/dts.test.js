// The .d.ts shape files beside src/ modules: what a cross-context payload actually carries,
// written down where an editor (and a reader) can find it. Nothing builds or emits from
// them — jsconfig.json is editor configuration only — so this is what keeps them honest:
// a declaration that names an export the module no longer has is drift, not documentation.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, readdirSync, existsSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import path from 'node:path';

const SRC = fileURLToPath(new URL('../src/', import.meta.url));
const read = (p) => readFileSync(p, 'utf8');

const walk = (dir, out = []) => {
  for (const e of readdirSync(dir, { withFileTypes: true })) {
    const p = path.join(dir, e.name);
    if (e.isDirectory()) walk(p, out);
    else if (e.name.endsWith('.d.ts')) out.push(p);
  }
  return out;
};
const FILES = walk(SRC).sort();
const rel = (p) => path.relative(SRC, p);

// The modules that carry a shape file. Pinned so adding one is a deliberate act — and so
// a rename cannot quietly drop a contract from the documented set.
const DOCUMENTED = [
  'background/ctxActions.d.ts',
  'background/editorRelay.d.ts',
  'background/frameCapture.d.ts',
  'background/handlers/ctxProbe.d.ts',
  'background/handlers/dropZones.d.ts',
  'background/handlers/editorMode.d.ts',
  'background/handlers/pageApi.d.ts',
  'background/menus.d.ts',
  'background/registrars.d.ts',
  'background/tabState.d.ts',
  'content/editorApiMain.d.ts',
  'crop/cropControls.d.ts',
  'crop/cropHandoff.d.ts',
  'crop/cropStage.d.ts',
  'lib/accent.d.ts',
  'lib/accentPreview.d.ts',
  'lib/actionIcon.d.ts',
  'lib/actionMenu.d.ts',
  'lib/chatDrop.d.ts',
  'lib/chatLayoutPrefs.d.ts',
  'lib/chatMsgMenu.d.ts',
  'lib/chatStatusTip.d.ts',
  'lib/chatUi.d.ts',
  'lib/collapsibleSections.d.ts',
  'lib/connectionModel.d.ts',
  'lib/connectionRest.d.ts',
  'lib/connectionStore.d.ts',
  'lib/connections.d.ts',
  'lib/contextMenu.d.ts',
  'lib/contextMenuItems.d.ts',
  'lib/controlTooltip.d.ts',
  'lib/cropGeometry.d.ts',
  'lib/customSelect.d.ts',
  'lib/displayName.d.ts',
  'lib/dragSections.d.ts',
  'lib/dragUrl.d.ts',
  'lib/dropChoice.d.ts',
  'lib/dropEntry.d.ts',
  'lib/dropZones.d.ts',
  'lib/dropdownMenu.d.ts',
  'lib/dustCloud.d.ts',
  'lib/dustGrains.d.ts',
  'lib/dustWake.d.ts',
  'lib/editorLaunch.d.ts',
  'lib/editorTabs.d.ts',
  'lib/escapeHtml.d.ts',
  'lib/filterUi.d.ts',
  'lib/filters.d.ts',
  'lib/fitWidest.d.ts',
  'lib/highlight.d.ts',
  'lib/highlightColor.d.ts',
  'lib/hoverHighlight.d.ts',
  'lib/hoverPreview.d.ts',
  'lib/icons.d.ts',
  'lib/imageData.d.ts',
  'lib/imageModel.d.ts',
  'lib/imageScan.d.ts',
  'lib/ledger.d.ts',
  'lib/logoAccent.d.ts',
  'lib/logoDragMenu.d.ts',
  'lib/messages.d.ts',
  'lib/motion/chatFx.d.ts',
  'lib/motion/disintegrate.d.ts',
  'lib/motion/enterLeave.d.ts',
  'lib/motion/painters.d.ts',
  'lib/motion/reveal.d.ts',
  'lib/motion/surfaceMotion.d.ts',
  'lib/motion/surfaces.d.ts',
  'lib/motion/tiles.d.ts',
  'lib/motion/tips.d.ts',
  'lib/motion/tune.d.ts',
  'lib/motionIcons.d.ts',
  'lib/motionPrefs.d.ts',
  'lib/numericInput.d.ts',
  'lib/onceGate.d.ts',
  'lib/openIn.d.ts',
  'lib/overlay.d.ts',
  'lib/pageImages.d.ts',
  'lib/pins.d.ts',
  'lib/pollClock.d.ts',
  'lib/popover.d.ts',
  'lib/prefs.d.ts',
  'lib/rasterize.d.ts',
  'lib/rowModel.d.ts',
  'lib/scrollbarHover.d.ts',
  'lib/sectionPeek.d.ts',
  'lib/settings.d.ts',
  'lib/shellPrefs.d.ts',
  'lib/shellTheme.d.ts',
  'lib/stencil.d.ts',
  'lib/swapGeometry.d.ts',
  'lib/themeSwap.d.ts',
  'lib/tip.d.ts',
  'lib/tipContent.d.ts',
  'lib/urlGuard.d.ts',
  'lib/videoFrames.d.ts',
  'llm/chatController.d.ts',
  'llm/chatListing.d.ts',
  'llm/llmClient.d.ts',
  'llm/llmSettings.d.ts',
  'llm/llmSurface.d.ts',
  'llm/opExecutors.d.ts',
  'llm/opPlan.d.ts',
  'llm/opProfile.d.ts',
  'llm/opPrompt.d.ts',
  'llm/opSchema.d.ts',
  'llm/opValidate.d.ts',
  'llm/openActions.d.ts',
  'options/confirmDialog.d.ts',
  'options/pinRow.d.ts',
  'options/pins.d.ts',
  'options/pinsDom.d.ts',
  'popup/assistant.d.ts',
  'popup/assistant/attachments.d.ts',
  'popup/assistant/boot.d.ts',
  'popup/assistant/capabilities.d.ts',
  'popup/assistant/composerMenu.d.ts',
  'popup/assistant/jumpPills.d.ts',
  'popup/assistant/msgMenu.d.ts',
  'popup/assistant/results.d.ts',
  'popup/assistant/shared.d.ts',
  'popup/assistant/transcript.d.ts',
  'popup/assistant/turnRunner.d.ts',
  'popup/dialogShell.d.ts',
  'popup/dragWiring.d.ts',
  'popup/editorDialogs.d.ts',
  'popup/editorHandle.d.ts',
  'popup/editorImport.d.ts',
  'popup/editorList.d.ts',
  'popup/editorMode.d.ts',
  'popup/filters.d.ts',
  'popup/gestures.d.ts',
  'popup/hoverHighlight.d.ts',
  'popup/model.d.ts',
  'popup/openActions.d.ts',
  'popup/panelDom.d.ts',
  'popup/pinActions.d.ts',
  'popup/pinDialog.d.ts',
  'popup/preview.d.ts',
  'popup/row.d.ts',
  'popup/rowMenu.d.ts',
  'popup/scan.d.ts',
  'popup/sections.d.ts',
  'popup/sharedPins.d.ts',
  'popup/sourceTabsList.d.ts',
];

test('the documented set is exactly the pinned list', () => {
  assert.deepEqual(FILES.map(rel), DOCUMENTED);
});

// `export declare const|function|class X` — the names a .d.ts claims the module exports.
const declaredValues = (src) =>
  [...src.matchAll(/^export declare (?:const|function|class) (\w+)/gm)].map((m) => m[1]);
// `export interface|type X` — documentation that emits nothing.
const declaredTypes = (src) =>
  [...src.matchAll(/^export (?:interface|type) (\w+)/gm)].map((m) => m[1]);

// What a module actually exports: direct declarations, re-exports, and `export {…}` lists.
const moduleExports = (src) => {
  const names = new Set();
  for (const m of src.matchAll(/^export (?:const|let|function|async function|class) (\w+)/gm)) names.add(m[1]);
  for (const m of src.matchAll(/^export\s*\{([^}]*)\}/gm)) {
    for (const part of m[1].split(',')) {
      const as = part.trim().split(/\s+as\s+/);
      if (as.length && as[as.length - 1]) names.add(as[as.length - 1].trim());
    }
  }
  return names;
};

for (const file of FILES) {
  const name = rel(file);
  const src = read(file);

  test(`${name}: stands beside its module and declares real shapes`, () => {
    const js = file.replace(/\.d\.ts$/, '.js');
    assert.ok(existsSync(js), `${name} documents no module — ${path.basename(js)} is gone`);
    assert.ok(declaredTypes(src).length + declaredValues(src).length,
      `${name} declares nothing — it documents nothing`);
    // A shape file declares; it never assigns, and so ships no behaviour.
    assert.ok(!/^export declare .*=[^>]/m.test(src),
      `${name} assigns a value — a .d.ts only declares`);
  });

  test(`${name}: every value it declares is still exported`, () => {
    const js = file.replace(/\.d\.ts$/, '.js');
    if (!existsSync(js)) return;
    const actual = moduleExports(read(js));
    const stale = declaredValues(src).filter((n) => !actual.has(n));
    assert.deepEqual(stale, [], `${name} declares exports the module no longer has`);
  });

  test(`${name}: every type it imports resolves to a shape file`, () => {
    for (const m of src.matchAll(/^import type .*? from '([^']+)'/gm)) {
      const spec = m[1];
      assert.match(spec, /^\.{1,2}\//, `${name}: "${spec}" is not a relative import`);
      const target = path.resolve(path.dirname(file), spec.replace(/\.js$/, '.d.ts'));
      assert.ok(existsSync(target), `${name}: "${spec}" has no shape file`);
    }
  });
}

test('jsconfig.json is editor configuration only, and reads the shape files', () => {
  const cfg = JSON.parse(read(fileURLToPath(new URL('../jsconfig.json', import.meta.url))));
  assert.equal(cfg.compilerOptions.noEmit, true, 'nothing may be emitted — this repo has no build step');
  assert.equal(cfg.compilerOptions.checkJs, false, 'the .js is not type-checked; the shapes are documentation');
  assert.equal(cfg.compilerOptions.allowJs, true);
  assert.ok(cfg.include.includes('src/**/*.d.ts'), 'the shape files must be in scope');
  assert.ok(cfg.include.includes('src/**/*.js'));
});
