// The .stc editor's shots: highlighting in both themes, completion, hover, diagnostics, a run in
// the terminal, and the two moving pictures — completion hints and an edit checked as it is typed.
import path from 'node:path';
import { scratchDir } from '../lib/paths.mjs';
import { pairNames } from '../lib/theme/selector.mjs';
import { film } from '../lib/shot/shots.mjs';
import { framesToGif } from '../lib/gifTools.mjs';
import { settle, waitForStable } from '../lib/waits.mjs';
import {
  EDITOR, PANEL, PARK, TIMEOUTS, TYPE_DELAY, clipOf, config, lineEnd, runner, still, stillTerminal,
  terminalText,
} from './view.mjs';

export const STC_STEPS = Object.freeze([
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
    await stillTerminal(ctx, 'run-terminal', ...EDITOR, PANEL);
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
]);

export const CHECK_STEPS = Object.freeze([
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
    // The retype raises the suggestion list; the clip holds on the fixed line, not on it.
    await ctx.page.keyboard.press('Escape');
    await ctx.page.locator('.squiggly-error').first().waitFor({ state: 'detached', timeout: TIMEOUTS.widgetMs }).catch(() => {});
    const shot = await filming;
    framesToGif(frames, path.join(runner.out, 'edit-and-check.gif'),
      { ...config.gifLook, inFps: shot.fps });
    console.log('  edit-and-check.gif');
    await ctx.host.runCommand('File: Revert File');
  } },
]);
