// The .stcjs facade's shots: member completion and hover, the palette filtered to this
// extension's commands, and the moving picture of a facade call typed with its hints.
import path from 'node:path';
import { scratchDir } from '../lib/paths.mjs';
import { film } from '../lib/shot/shots.mjs';
import { framesToGif } from '../lib/gifTools.mjs';
import { settle, waitForStable } from '../lib/waits.mjs';
import { EDITOR, PALETTE, PARK, TIMEOUTS, TYPE_DELAY, config, lineEnd, runner, still } from './view.mjs';

export const API_STEPS = Object.freeze([
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
]);

export const PALETTE_STEPS = Object.freeze([
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
]);
