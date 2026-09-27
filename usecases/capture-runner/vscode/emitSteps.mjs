// The shots of the files this extension gives: the explorer's per-type icons, the emit pick of
// target languages, and an emitted .pystc running on its own interpreter.
import { SAMPLES } from '../lib/vscodeHost.mjs';
import { quantizePng } from '../lib/gifTools.mjs';
import { settle, waitForStable } from '../lib/waits.mjs';
import {
  EDITOR, FILE_ROW, LABEL_GUTTER, PALETTE, PANEL, TIMEOUTS, runner, still, stillTerminal, terminalText,
} from './view.mjs';

export const EMIT_STEPS = Object.freeze([
  // The explorer's file list alone — one file per type, no editor around it: the shot is about
  // the icons this extension gives, so it is cropped to the SAMPLE rows. The workspace also
  // holds a picture for the other scenarios, whose icon is VS Code's own and not the subject.
  { name: 'file-icons', run: async (ctx) => {
    // With a file open the explorer reveals and SELECTS its row, which lands in the shot
    // highlighted; this one is about the icons, not about what is open.
    await ctx.host.runCommand('View: Close All Editors');
    await ctx.page.keyboard.press('Meta+Shift+E');
    const rows = ctx.page.locator(`${FILE_ROW}:not([aria-expanded])`);
    await rows.first().waitFor({ timeout: TIMEOUTS.widgetMs });
    await ctx.page.keyboard.press('Escape');
    await settle(400);
    // Folders (aria-expanded) sit above the files — the workspace's own out/ is not the
    // subject — and the pane is far wider than the names, so the crop follows the labels.
    const clip = await ctx.page.evaluate(([selector, gutter, names]) => {
      const rows = [...document.querySelectorAll(selector)]
        .filter((r) => names.includes((r.querySelector('.label-name')?.textContent || '').trim()));
      const box = (node) => node.getBoundingClientRect();
      const right = Math.max(...rows.map((r) => box(r.querySelector('.label-name') || r).right));
      const first = box(rows[0]);
      return {
        x: Math.round(first.x), y: Math.round(first.y),
        width: Math.round(right - first.x) + gutter,
        height: Math.round(box(rows[rows.length - 1]).bottom - first.y),
      };
    }, [`${FILE_ROW}:not([aria-expanded])`, LABEL_GUTTER, SAMPLES]);
    quantizePng(await runner.shot(ctx.page, 'file-icons', { clip }));
  } },
  // The emit pick: the four suffixes, which are the language choice — the CLI reads the
  // target off the output's extension.
  { name: 'emit-targets', run: async (ctx) => {
    await ctx.host.openFile('example.stc');
    await ctx.host.runCommand('Stencil: Emit script as');
    await ctx.page.locator(`${PALETTE} .monaco-list-row`).first().waitFor({ timeout: TIMEOUTS.widgetMs });
    await settle(400);
    await still(ctx, 'emit-targets', PALETTE);
    await ctx.page.keyboard.press('Escape');
  } },
  // The emitted file running on its own interpreter: the .pystc's own Run button, and what
  // it printed in the terminal.
  { name: 'python-run', run: async (ctx) => {
    await ctx.host.openFile('example.pystc');
    await ctx.host.runCommand('Stencil: Run Python script');
    await ctx.page.locator('.terminal-wrapper').first().waitFor({ timeout: TIMEOUTS.terminalMs });
    await ctx.page.waitForFunction(() => /wrote |Error|error/.test(
      document.querySelector('.terminal-wrapper.active .xterm-rows')?.innerText ?? ''), null, { timeout: 60_000 });
    await waitForStable(ctx.page, terminalText, { idleMs: 500, timeoutMs: 20_000 });
    await stillTerminal(ctx, 'python-run', ...EDITOR, PANEL);
    await ctx.page.keyboard.press('Meta+J');
  } },
]);
