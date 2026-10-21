// The shots about the CLI behind the run commands: the setting that names it, what a run says
// without it, a source-less script run on a picked picture, and the check that runs nothing.
import fs from 'node:fs';
import path from 'node:path';
import { CAPTURE } from '../lib/paths.mjs';
import { WORKSPACE } from '../lib/vscodeHost.mjs';
import { settle } from '../lib/waits.mjs';
import {
  EDITOR, PANEL, TIMEOUTS, cleanRun, config, getSetting, lineEnd, noSquiggles, openNamed, setSetting, still,
  stillTerminal,
} from './view.mjs';

const SAMPLES = config.get('samples');
const sampleTo = (name) =>
  fs.copyFileSync(path.join(CAPTURE, 'vscode', 'sample', name), path.join(WORKSPACE(config), name));

export const CLI_STEPS = Object.freeze([
  { name: 'cli-setting', run: async (ctx) => {
    await ctx.host.runCommand('Preferences: Open User Settings');
    const search = ctx.page.locator('.settings-editor .search-container');
    await search.waitFor({ timeout: TIMEOUTS.widgetMs });
    await search.click();
    await ctx.page.keyboard.type('stencil.cliPath');
    await ctx.page.locator('.settings-editor .setting-item-contents', { hasText: 'Cli Path' }).first()
      .waitFor({ timeout: TIMEOUTS.widgetMs });
    await settle(600);
    await still(ctx, 'cli-setting', '.settings-editor .search-container', '.settings-editor .setting-item-contents');
    // The search box keeps F1 from the palette; the editor's own close key reaches it.
    await ctx.page.keyboard.press('Meta+W');
  } },
  // No setting, no STENCIL_CLI and nothing on PATH: the run stops and names the setting.
  { name: 'cli-missing', run: async (ctx) => {
    const configured = getSetting('stencil.cliPath');
    await setSetting('stencil.cliPath', '');
    await ctx.host.openFile('example.stc');
    // Its own key: typed into the palette, "Run script" also matches "Run Python script".
    await ctx.page.locator('.view-line').first().click();
    await ctx.page.keyboard.press('Meta+Alt+R');
    const toast = ctx.page.locator('.notifications-toasts .notification-toast', { hasText: 'Stencil CLI not found' });
    await toast.first().waitFor({ timeout: TIMEOUTS.widgetMs });
    await settle(400);
    await still(ctx, 'cli-missing', '.notifications-toasts .notification-toast');
    await ctx.host.runCommand('Notifications: Clear All Notifications');
    await setSetting('stencil.cliPath', configured);
  } },
  // A script that names no @source, run on a picture picked in VS Code's own file dialog.
  { name: 'run-on-image', run: async (ctx) => {
    sampleTo(SAMPLES.unsourced);
    await openNamed(ctx, SAMPLES.unsourced);
    await cleanRun(ctx, async () => {
      await ctx.host.runCommand('Stencil: Run script on an image');
      const input = ctx.page.locator('.quick-input-widget input');
      await input.waitFor({ timeout: TIMEOUTS.widgetMs });
      await input.press('End');
      await ctx.page.keyboard.type(SAMPLES.picture);
      await ctx.page.keyboard.press('Enter');
    }, /wrote |error:/);
    await stillTerminal(ctx, 'run-on-image', ...EDITOR, PANEL);
    await ctx.page.keyboard.press('Meta+J');
  } },
  // The diagnostics shot's typo, saved and handed to the CLI's own check.
  { name: 'check-script', run: async (ctx) => {
    await openNamed(ctx, 'example.stc');
    await lineEnd(ctx, '@filter sepia');
    await ctx.page.keyboard.press('Shift+Home');
    await ctx.page.keyboard.type('@filtre sepia');
    await cleanRun(ctx, () => ctx.host.runCommand('Stencil: Check script'), /E_UNKNOWN_DIRECTIVE|error:/);
    await stillTerminal(ctx, 'check-script', ...EDITOR, PANEL);
    await ctx.page.keyboard.press('Meta+J');
    sampleTo('example.stc');
    await ctx.host.runCommand('File: Revert File');
    await noSquiggles(ctx);
  } },
]);
