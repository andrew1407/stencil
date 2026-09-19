// The shots for the commands this extension puts on the editor itself: the title-bar action icons
// (package.json editor/title) and the expression box "Run selection in Stencil Web Console" raises.
import { quantizePng } from '../lib/gifTools.mjs';
import { settle } from '../lib/waits.mjs';

export function makeActionSteps({ config, runner, still }) {
  const timeouts = config.get('timeouts');
  const pad = config.get('titleActions.padPx');

  return Object.freeze([
    // package.json editor/title: a .stc script carries all three — Run script, Open in Stencil Web
    // and its incognito twin — while a .stcjs file gets only Run in Stencil Web Console and a
    // .stencil project the two Open ones. The shot takes the script, so all three are on it.
    {
      name: 'title-actions',
      run: async (ctx) => {
        await ctx.host.openFile('example.stc');
        await ctx.page.locator('.editor-actions .action-item').first()
          .waitFor({ timeout: timeouts.widgetMs });
        await settle(600);
        // The tab strip only: its own band, from the first tab to the last action.
        const clip = await ctx.page.evaluate((gutter) => {
          const row = document.querySelector('.editor-actions').getBoundingClientRect();
          const tabs = document.querySelector('.tabs-container').getBoundingClientRect();
          return {
            x: Math.round(tabs.x),
            y: Math.round(tabs.y),
            width: Math.round(row.right - tabs.x) + gutter,
            height: Math.round(tabs.height),
          };
        }, pad);
        quantizePng(await runner.shot(ctx.page, 'title-actions', { clip }));
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
        await still(ctx, 'run-selection');
        await ctx.page.keyboard.press('Escape');
      },
    },
  ]);
}
