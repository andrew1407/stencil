// The shot for a script travelling from VS Code to the Stencil apps: the .stc title bar's "…" menu
// with the two hand-offs that open without running — the web app's Script window and the desktop's.
import fs from 'node:fs';
import path from 'node:path';
import { CAPTURE } from '../lib/paths.mjs';
import { WORKSPACE } from '../lib/vscodeHost.mjs';
import { settle } from '../lib/waits.mjs';
import { PARK, TIMEOUTS, config, still } from './view.mjs';

const HANDOFF = config.get('handoff');
const MORE = '.editor-actions .action-item .codicon-toolbar-more';
const MENU = '.context-view .monaco-menu';

export const HANDOFF_STEPS = Object.freeze([
  { name: 'handoff-commands', run: async (ctx) => {
    fs.copyFileSync(path.join(CAPTURE, 'vscode', 'sample', 'handoff.stc'), path.join(WORKSPACE(config), HANDOFF.file));
    await ctx.host.openFile(HANDOFF.file);
    await ctx.page.locator(MORE).first().click();
    await ctx.page.locator(`${MENU} .action-label`, { hasText: 'Open in Stencil Desktop' }).first()
      .waitFor({ timeout: TIMEOUTS.widgetMs });
    await settle(HANDOFF.settleMs);
    await still(ctx, 'handoff-commands', MENU, '.editor-actions', '.tabs-container');
    await ctx.page.keyboard.press('Escape');
    await ctx.page.mouse.move(PARK.x, PARK.y);
  } },
]);
