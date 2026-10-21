// The browser-console route photographed end to end: the app served from this checkout, opened
// under VS Code's own JavaScript debugger, a picture loaded and two expressions run in the page,
// every answer landing in the Stencil output channel.
import { startAppServer } from '../lib/servers.mjs';
import { settle } from '../lib/waits.mjs';
import { TIMEOUTS, config, getSetting, setSetting, still } from './view.mjs';

const CONSOLE = config.get('console');
const OUTPUT_LINE = '.part.panel .view-lines .view-line';

const printed = (selector) => [...document.querySelectorAll(selector)].filter((l) => l.textContent.trim()).length;

// Types into the box a console command raises; the channel answers with the `>` echo and then
// the result, so the step waits for both lines.
const ask = async (ctx, command, typed) => {
  const before = await ctx.page.evaluate(printed, OUTPUT_LINE);
  await ctx.host.runCommand(command);
  const input = ctx.page.locator('.quick-input-widget input');
  await input.waitFor({ timeout: TIMEOUTS.widgetMs });
  await input.fill(typed);
  await ctx.page.keyboard.press('Enter');
  await ctx.page.waitForFunction(([selector, want]) => [...document.querySelectorAll(selector)]
    .filter((l) => l.textContent.trim()).length >= want, [OUTPUT_LINE, before + 2], { timeout: CONSOLE.replyMs });
};

export const CONSOLE_STEPS = Object.freeze([
  { name: 'console-output', run: async (ctx) => {
    // A port of its own, so a browser capture running beside this one keeps its server.
    process.env.APP_PORT ||= String(CONSOLE.appPort);
    const app = await startAppServer();
    const before = getSetting('stencil.webUrl');
    await setSetting('stencil.webUrl', app.url);
    try {
      await ctx.host.openFile('example.stcjs');
      await ask(ctx, 'Stencil: Open an image in Stencil Web', config.url('botIcon'));
      await ask(ctx, 'Stencil: Run selection in Stencil Web Console', CONSOLE.expression);
      await settle(1200);
      await still(ctx, 'console-output', '.part.panel .composite.title', OUTPUT_LINE);
    } catch (err) {
      console.warn(`  console-output skipped: ${err.message.split('\n')[0]}`);
    } finally {
      // Stop's own key acts only on a live session; a debug command typed into the palette could
      // start one for the .stcjs on screen and raise VS Code's "find a debugger" dialog.
      await ctx.page.keyboard.press('Shift+F5');
      await settle(800);
      await setSetting('stencil.webUrl', before);
      app.stop();
    }
  } },
]);
