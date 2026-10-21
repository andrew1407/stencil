// The shots for the commands this extension puts on the editor itself: the title-bar action icons
// (package.json editor/title) and the expression box "Run selection in Stencil Web Console" raises.
import { execFileSync } from 'node:child_process';
import path from 'node:path';
import { quantizePng } from '../lib/gifTools.mjs';
import { scratchDir } from '../lib/paths.mjs';
import { settle } from '../lib/waits.mjs';
import { clipOf, resizeWindow } from './view.mjs';

// One title row per file type, in the order the doc names them.
const TITLE_FILES = Object.freeze(['example.stc', 'example.stcjs', 'example.stencil']);

export function makeActionSteps({ config, runner, still }) {
  const timeouts = config.get('timeouts');

  return Object.freeze([
    // package.json editor/title: a .stc carries Run script and both web and both desktop hand-offs,
    // a .stcjs only Run in Stencil Web Console, a .stencil project the two web ones. In a narrowed
    // window each file's own tab sits near its buttons, and the three real rows are stacked.
    {
      name: 'title-actions',
      run: async (ctx) => {
        const rows = scratchDir('vs-title-rows');
        await ctx.host.runCommand('View: Close Primary Side Bar');
        await resizeWindow(ctx, config.get('narrowWindow'));
        const files = [];
        for (const file of TITLE_FILES) {
          await ctx.host.openFile(file);
          await ctx.host.runCommand('View: Close Other Editors in Group');
          await ctx.page.locator('.editor-actions .action-item').first().waitFor({ timeout: timeouts.widgetMs });
          await settle(600);
          const row = path.join(rows, `${files.length}.png`);
          const clip = await clipOf(ctx.page, '.tabs-and-actions-container');
          clip.height -= config.get('cropPadPx');   // not the breadcrumb row beneath
          await ctx.page.screenshot({ path: row, clip });
          files.push(row);
        }
        const out = path.join(runner.out, 'title-actions.png');
        execFileSync('ffmpeg', ['-hide_banner', '-loglevel', 'error', '-y', ...files.flatMap((f) => ['-i', f]),
          '-filter_complex', `${files.map((_, i) => `[${i}:v]`).join('')}vstack=inputs=${files.length}`, out]);
        quantizePng(out);
        console.log('  title-actions.png');
        await resizeWindow(ctx);
        await ctx.host.openFile('example.stc');
      },
    },
    // With nothing selected the command asks for the expression instead of running one
    // (webCommands.js runSelectionInWebConsole), and that box is what says what it expects.
    {
      name: 'run-selection',
      run: async (ctx) => {
        await ctx.host.openFile('example.stcjs');
        await ctx.page.keyboard.press('Escape');
        await ctx.host.runCommand('Stencil: Run selection in Stencil Web Console');
        await ctx.page.locator('.quick-input-widget input').waitFor({ timeout: timeouts.widgetMs });
        await settle(400);
        await still(ctx, 'run-selection', '.quick-input-widget', '.tabs-container', '.view-line');
        await ctx.page.keyboard.press('Escape');
      },
    },
  ]);
}
