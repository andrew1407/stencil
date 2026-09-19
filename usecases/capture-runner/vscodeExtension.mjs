// Captures usecases/docs/vscode-extension/img/* from a real VS Code running the extension out of
// the tree (--extensionDevelopmentPath), driven over its Chromium debug port. One launch per
// theme the shots ask for; constants live in config/vscodeExtension.json.
//   node usecases/capture-runner/vscodeExtension.mjs [--only <name>]
import path from 'node:path';
import { loadCaptureConfig } from './lib/captureConfig.mjs';
import { outDir, scratchDir } from './lib/paths.mjs';
import { makeShotRunner } from './lib/shotRunner.mjs';
import { pairNames } from './lib/themeSelector.mjs';
import { VsCodeHost } from './lib/vscodeHost.mjs';
import { film } from './lib/shots.mjs';
import { framesToGif, quantizePng } from './lib/gifTools.mjs';
import { settle, waitForStable } from './lib/waits.mjs';
import { makeActionSteps } from './vscode/actionSteps.mjs';

const config = loadCaptureConfig('vscodeExtension');
const runner = makeShotRunner({ config, out: outDir('vscode-extension') });
const TIMEOUTS = config.get('timeouts');
const TYPE_DELAY = config.get('typeDelayMs');

// A VS Code window is mostly chrome the feature has nothing to do with, so every shot crops to
// the parts that carry it: the union box of the selectors it is handed, plus a small margin.
const clipOf = async (page, ...selectors) => page.evaluate(([sels, pad]) => {
  const boxes = sels.flatMap((sel) => [...document.querySelectorAll(sel)]
    .map((node) => node.getBoundingClientRect())
    .filter((b) => b.width > 1 && b.height > 1));
  const x = Math.max(0, Math.min(...boxes.map((b) => b.x)) - pad);
  const y = Math.max(0, Math.min(...boxes.map((b) => b.y)) - pad);
  return {
    x: Math.round(x),
    y: Math.round(y),
    width: Math.round(Math.min(window.innerWidth, Math.max(...boxes.map((b) => b.right)) + pad) - x),
    height: Math.round(Math.min(window.innerHeight, Math.max(...boxes.map((b) => b.bottom)) + pad) - y),
  };
}, [selectors, config.get('cropPadPx')]);

// The editor group without the activity bar, the side bar, the window title or the status bar —
// and bounded by the RENDERED lines rather than the pane, so a short file leaves no empty half.
const EDITOR = ['.tabs-container', '.view-line'];
const PANEL = '.part.panel';
const PALETTE = '.quick-input-widget';

const still = async (ctx, name, ...selectors) => quantizePng(
  await runner.shot(ctx.page, name,
    selectors.length ? { clip: await clipOf(ctx.page, ...selectors) } : undefined));
const lineEnd = async (ctx, text) => {
  await ctx.page.locator('.view-line', { hasText: text }).first().click();
  await ctx.page.keyboard.press('End');
};
const terminalText = () => document.querySelector('.terminal-wrapper.active .xterm-rows')?.innerText ?? '';
const FILE_ROW = '.explorer-folders-view .monaco-list-row';
const LABEL_GUTTER = config.get('fileIcons.labelGutterPx');
// Empty editor space: a pointer resting anywhere else pops that control's tooltip into the
// next shot — the activity bar's, when the window opens under the real cursor.
const PARK = config.get('pointerPark');

const STEPS = Object.freeze([
  ...pairNames('highlighting').map((name) => ({ name, run: (ctx) => still(ctx, name, ...EDITOR) })),
  { name: 'completion', run: async (ctx) => {
    await lineEnd(ctx, '@filter sepia');
    await ctx.page.keyboard.press('Enter');
    await ctx.page.keyboard.type('@fi', { delay: TYPE_DELAY });
    await ctx.page.locator('.suggest-widget.visible').waitFor({ timeout: 5000 })
      .catch(() => ctx.page.keyboard.press('Control+Space'));
    await ctx.page.locator('.suggest-widget.visible .monaco-list-row').first().waitFor({ timeout: TIMEOUTS.widgetMs });
    await still(ctx, 'completion', ...EDITOR);
    await ctx.page.keyboard.press('Escape');
    await ctx.host.runCommand('File: Revert File');
  } },
  // A synthetic pointer never rests long enough for the editor's hover: ask for it at the caret.
  { name: 'hover', run: async (ctx) => {
    await ctx.page.locator('.view-line span', { hasText: /^@crop$/ }).first().click();
    await ctx.host.runCommand('Show or Focus Hover');
    await ctx.page.locator('.monaco-hover:not(.hidden)').first().waitFor({ timeout: TIMEOUTS.widgetMs });
    await waitForStable(ctx.page, () => document.querySelector('.monaco-hover')?.innerText ?? '',
      { idleMs: 400, timeoutMs: TIMEOUTS.widgetMs });
    await still(ctx, 'hover', ...EDITOR);
    await ctx.page.mouse.move(PARK.x, PARK.y);
  } },
  { name: 'diagnostics', run: async (ctx) => {
    await lineEnd(ctx, '@use px');
    await ctx.page.keyboard.press('Enter');
    await ctx.page.keyboard.type('@filtre sepai', { delay: TYPE_DELAY });
    await ctx.page.locator('.squiggly-error').first().waitFor({ timeout: TIMEOUTS.widgetMs });
    await ctx.page.keyboard.press('Meta+Shift+M');
    await ctx.page.locator('.markers-panel .monaco-list-row').first().waitFor({ timeout: TIMEOUTS.widgetMs });
    await still(ctx, 'diagnostics', ...EDITOR, PANEL);
    await ctx.page.keyboard.press('Meta+J');
    await ctx.host.runCommand('File: Revert File');
  } },
  // The run is over when the binary prints what it wrote. The first run also creates the terminal,
  // so clear it and run again for a shot that reads as a user sees it.
  { name: 'run-terminal', run: async (ctx) => {
    const ran = () => ctx.page.waitForFunction(() => /wrote |error:/.test(
      document.querySelector('.terminal-wrapper.active .xterm-rows')?.innerText ?? ''), null, { timeout: 60_000 });
    const trigger = async () => {
      await ctx.page.locator('.view-line', { hasText: '@save' }).first().click();
      await ctx.page.keyboard.press('Meta+Alt+R');
    };
    await trigger();
    await ctx.page.locator('.terminal-wrapper').first().waitFor({ timeout: TIMEOUTS.terminalMs });
    await ran();
    await ctx.page.locator('.terminal-wrapper.active .xterm').first().click();
    await ctx.page.keyboard.type('clear');
    await ctx.page.keyboard.press('Enter');
    await waitForStable(ctx.page, terminalText, { idleMs: 400, timeoutMs: 10_000 });
    await trigger();
    await ran();
    await waitForStable(ctx.page, terminalText, { idleMs: 500, timeoutMs: 20_000 });
    await still(ctx, 'run-terminal', ...EDITOR, PANEL);
    await ctx.page.keyboard.press('Meta+J');
  } },
  // Typing into the file, the suggestion list opening and narrowing, a pick accepted, and
  // the next statement's own suggestions — the section's moving picture.
  { name: 'completion-hints', run: async (ctx) => {
    const clip = config.get('clips.completion-hints');
    const frames = scratchDir('vs-hints');
    const suggestions = ctx.page.locator('.suggest-widget.visible');
    const offer = async (typed) => {
      await ctx.page.keyboard.type(typed, { delay: clip.typeDelayMs });
      await suggestions.waitFor({ timeout: 2500 }).catch(() => ctx.page.keyboard.press('Control+Space'));
      await settle(clip.holdMs);
    };
    await lineEnd(ctx, '@filter sepia');
    await ctx.page.keyboard.press('Enter');
    // The code alone: from the breadcrumb down (no tab bar), stopping short of the overview
    // ruler, so neither window chrome nor the scrollbar strip reaches the picture.
    const editor = await ctx.page.locator('.part.editor').first().boundingBox();
    const from = await ctx.page.locator(clip.clipFrom).first().boundingBox();
    const region = { clip: {
      x: editor.x, y: from.y, width: editor.width - clip.clipTrimRight, height: clip.clipHeight,
    } };
    const filming = film(ctx.page, frames, clip.ms, clip.everyMs, region);
    await settle(clip.leadMs);
    await offer('@');
    await offer('re');                                        // the list narrows to @rect
    await ctx.page.keyboard.press('Enter');                   // …and the pick is accepted
    await ctx.page.keyboard.type(' (10%, 10%) (90%, 90%)', { delay: clip.typeDelayMs });
    await settle(clip.holdMs);
    await ctx.page.keyboard.press('Enter');
    await offer('@filter se');                                // inside a statement: what is legal there
    await ctx.page.keyboard.press('Escape');
    const shot = await filming;
    framesToGif(frames, path.join(runner.out, 'completion-hints.gif'),
      { ...config.gifLook, inFps: shot.fps });
    console.log('  completion-hints.gif');
    await ctx.host.runCommand('File: Revert File');
  } },
  // The explorer's file list alone — one file per type, no editor around it: the shot is about the
  // icons, so it is cropped to the rows that carry them.
  { name: 'file-icons', run: async (ctx) => {
    await ctx.page.keyboard.press('Meta+Shift+E');
    const rows = ctx.page.locator(`${FILE_ROW}:not([aria-expanded])`);
    await rows.first().waitFor({ timeout: TIMEOUTS.widgetMs });
    // Escape is the list's `clear`: the open file's revealed row would otherwise sit in the
    // shot highlighted, and this one is about the icons, not about what is open.
    await ctx.page.keyboard.press('Escape');
    await settle(400);
    // Folders (aria-expanded) sit above the files — the workspace's own out/ is not the
    // subject — and the pane is far wider than the names, so the crop follows the labels.
    const clip = await ctx.page.evaluate(([selector, gutter]) => {
      const rows = [...document.querySelectorAll(selector)];
      const box = (node) => node.getBoundingClientRect();
      const right = Math.max(...rows.map((r) => box(r.querySelector('.label-name') || r).right));
      const first = box(rows[0]);
      return {
        x: Math.round(first.x), y: Math.round(first.y),
        width: Math.round(right - first.x) + gutter,
        height: Math.round(box(rows[rows.length - 1]).bottom - first.y),
      };
    }, [`${FILE_ROW}:not([aria-expanded])`, LABEL_GUTTER]);
    quantizePng(await runner.shot(ctx.page, 'file-icons', { clip }));
  } },
  { name: 'api-completion', run: async (ctx) => {
    await ctx.host.openFile('example.stcjs');
    await lineEnd(ctx, 'stencil.rotateRight()');
    await ctx.page.keyboard.press('Enter');
    await ctx.page.keyboard.type('stencil.', { delay: TYPE_DELAY });
    await ctx.page.locator('.suggest-widget.visible').waitFor({ timeout: 5000 })
      .catch(() => ctx.page.keyboard.press('Control+Space'));
    await ctx.page.locator('.suggest-widget.visible .monaco-list-row').first().waitFor({ timeout: TIMEOUTS.widgetMs });
    await settle(500);
    await still(ctx, 'api-completion', ...EDITOR);
    await ctx.page.keyboard.press('Escape');
    await ctx.host.runCommand('File: Revert File');
  } },
  { name: 'api-hover', run: async (ctx) => {
    await ctx.host.openFile('example.stcjs');
    await ctx.page.locator('.view-line span', { hasText: /^setLines$/ }).first().click();
    await ctx.host.runCommand('Show or Focus Hover');
    await ctx.page.locator('.monaco-hover:not(.hidden)').first().waitFor({ timeout: TIMEOUTS.widgetMs });
    await waitForStable(ctx.page, () => document.querySelector('.monaco-hover')?.innerText ?? '',
      { idleMs: 400, timeoutMs: TIMEOUTS.widgetMs });
    await still(ctx, 'api-hover', ...EDITOR);
    await ctx.page.mouse.move(PARK.x, PARK.y);
  } },
  ...makeActionSteps({ config, runner, still }),
  // The palette filtered to this extension: the CLI commands and the four browser ones.
  { name: 'web-commands', run: async (ctx) => {
    await ctx.page.keyboard.press('F1');
    const input = ctx.page.locator('.quick-input-widget input');
    await input.waitFor({ timeout: TIMEOUTS.widgetMs });
    await input.fill('>Stencil: ');
    await ctx.page.locator('.quick-input-list .monaco-list-row').first().waitFor({ timeout: TIMEOUTS.widgetMs });
    await settle(400);
    await still(ctx, 'web-commands', PALETTE, ...EDITOR);
    await ctx.page.keyboard.press('Escape');
  } },
  // Typing a facade call: the member list opening, narrowing, a pick accepted with its
  // explanation beside it — the .stcjs section's moving picture.
  { name: 'api-hints', run: async (ctx) => {
    const clip = config.get('clips.api-hints');
    const frames = scratchDir('vs-api');
    const suggestions = ctx.page.locator('.suggest-widget.visible');
    const offer = async (typed) => {
      await ctx.page.keyboard.type(typed, { delay: clip.typeDelayMs });
      await suggestions.waitFor({ timeout: 2500 }).catch(() => ctx.page.keyboard.press('Control+Space'));
      await settle(clip.holdMs);
    };
    await ctx.host.openFile('example.stcjs');
    await lineEnd(ctx, 'stencil.rotateRight()');
    await ctx.page.keyboard.press('Enter');
    const editor = await ctx.page.locator('.part.editor').first().boundingBox();
    const from = await ctx.page.locator(clip.clipFrom).first().boundingBox();
    const region = { clip: {
      x: editor.x, y: from.y, width: editor.width - clip.clipTrimRight, height: clip.clipHeight,
    } };
    const filming = film(ctx.page, frames, clip.ms, clip.everyMs, region);
    await settle(clip.leadMs);
    await offer('stencil.');
    await offer('zoomF');                                     // the list narrows to zoomFit
    await ctx.page.keyboard.press('Enter');
    await ctx.page.keyboard.type('();', { delay: clip.typeDelayMs });
    await settle(clip.holdMs);
    await ctx.page.keyboard.press('Enter');
    await offer('stencil.download');                          // and the next call offers its own
    await ctx.page.keyboard.press('Escape');
    const shot = await filming;
    framesToGif(frames, path.join(runner.out, 'api-hints.gif'),
      { ...config.gifLook, inFps: shot.fps });
    console.log('  api-hints.gif');
    await ctx.host.runCommand('File: Revert File');
    await ctx.host.openFile('example.stc');
  } },
  { name: 'edit-and-check', run: async (ctx) => {
    const clip = config.get('clips.edit-and-check');
    const frames = scratchDir('vs-frames');
    await lineEnd(ctx, '@use px');
    await ctx.page.keyboard.press('Enter');
    const filming = film(ctx.page, frames, clip.ms, clip.everyMs,
      { clip: await clipOf(ctx.page, ...EDITOR) });
    await ctx.page.keyboard.type('@filtre invert', { delay: clip.typeDelayMs });
    await ctx.page.locator('.squiggly-error').first().waitFor({ timeout: TIMEOUTS.widgetMs }).catch(() => {});
    await settle(clip.holdMs);
    for (let i = 0; i < 'tre invert'.length; i++) await ctx.page.keyboard.press('Backspace');
    await ctx.page.keyboard.type('ter invert', { delay: clip.typeDelayMs });
    const shot = await filming;
    framesToGif(frames, path.join(runner.out, 'edit-and-check.gif'),
      { ...config.gifLook, inFps: shot.fps });
    console.log('  edit-and-check.gif');
    await ctx.host.runCommand('File: Revert File');
  } },
]);

// One VS Code per theme, dark first: a colour theme is a launch-time setting.
for (const theme of ['dark', 'light']) {
  const steps = STEPS.filter((step) => runner.wanted(step.name) && runner.themeOf(step.name) === theme);
  if (!steps.length) continue;
  console.log(`vscode extension, ${theme}`);
  const host = await VsCodeHost.launch(config, theme);
  await host.page.mouse.move(PARK.x, PARK.y);
  await runner.play(steps, { host, page: host.page });
  await host.stop();
}
runner.finish();
