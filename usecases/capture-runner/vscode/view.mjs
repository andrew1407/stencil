// The VS Code capture's frame: its config and shot runner, and the crops every shot is cut to —
// the editor group bounded by its rendered lines, the panel, the palette, the terminal down to its
// last written row — plus the caret, pointer-park, settings and window-size helpers the steps share.
import fs from 'node:fs';
import path from 'node:path';
import { loadCaptureConfig } from '../lib/captureConfig.mjs';
import { outDir } from '../lib/paths.mjs';
import { makeShotRunner } from '../lib/shot/runner.mjs';
import { quantizePng } from '../lib/gifTools.mjs';
import { settle, waitForStable } from '../lib/waits.mjs';
import { VS_DIR } from '../lib/vscodeHost.mjs';

export const config = loadCaptureConfig('vscodeExtension');
export const runner = makeShotRunner({ config, out: outDir('vscode-extension') });
export const TIMEOUTS = config.get('timeouts');
export const TYPE_DELAY = config.get('typeDelayMs');

// A VS Code window is mostly chrome the feature has nothing to do with, so every shot crops to
// the parts that carry it: the union box of the selectors it is handed, plus a small margin.
export const clipOf = async (page, ...selectors) => page.evaluate(([sels, pad]) => {
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
export const EDITOR = ['.tabs-container', '.view-line'];
export const PANEL = '.part.panel';
export const PALETTE = '.quick-input-widget';

export const still = async (ctx, name, ...selectors) => quantizePng(
  await runner.shot(ctx.page, name,
    selectors.length ? { clip: await clipOf(ctx.page, ...selectors) } : undefined));
// The terminal panel is a fixed pane, so it keeps a screenful of blank rows under whatever ran:
// a console shot ends at the last row that has anything on it.
export const stillTerminal = async (ctx, name, ...selectors) => {
  const clip = await clipOf(ctx.page, ...selectors);
  const bottom = await ctx.page.evaluate((pad) => {
    const rows = [...document.querySelectorAll('.terminal-wrapper.active .xterm-rows > div')];
    const last = rows.filter((r) => r.textContent.trim()).pop();
    return last ? Math.round(last.getBoundingClientRect().bottom + pad) : 0;
  }, config.get('cropPadPx'));
  if (bottom > clip.y && bottom < clip.y + clip.height) clip.height = bottom - clip.y;
  return quantizePng(await runner.shot(ctx.page, name, { clip }));
};
export const lineEnd = async (ctx, text) => {
  await ctx.page.locator('.view-line', { hasText: text }).first().click();
  await ctx.page.keyboard.press('End');
};
export const terminalText = () => document.querySelector('.terminal-wrapper.active .xterm-rows')?.innerText ?? '';
export const FILE_ROW = '.explorer-folders-view .monaco-list-row';
export const LABEL_GUTTER = config.get('fileIcons.labelGutterPx');
// Empty editor space: a pointer resting anywhere else pops that control's tooltip into the
// next shot — the activity bar's, when the window opens under the real cursor.
export const PARK = config.get('pointerPark');

// A user setting changed the way a user does: in the settings file VS Code watches. No event says
// it was read back, so the pause is declared (config `settingsMs`).
export const setSetting = async (key, value) => {
  const file = path.join(VS_DIR(config), 'user', 'User', 'settings.json');
  const settings = JSON.parse(fs.readFileSync(file, 'utf8'));
  if (value === undefined) delete settings[key];
  else settings[key] = value;
  fs.writeFileSync(file, JSON.stringify(settings, null, 2));
  await settle(config.get('settingsMs'));
};
export const getSetting = (key) =>
  JSON.parse(fs.readFileSync(path.join(VS_DIR(config), 'user', 'User', 'settings.json'), 'utf8'))[key];

// The window at another size (config `narrowWindow`), or back at the launch size with none.
export const resizeWindow = async (ctx, size = config.vscode.window) => {
  const cdp = await ctx.page.context().newCDPSession(ctx.page);
  await cdp.send('Emulation.setDeviceMetricsOverride', { ...size, deviceScaleFactor: 1, mobile: false });
  await cdp.detach();
  await settle(config.get('settingsMs'));
};

// Quick Open for a file the step itself just wrote: Enter only once the list offers THAT file, and
// done once its tab is the active one.
export const openNamed = async (ctx, name) => {
  await ctx.page.keyboard.press('F1');
  const input = ctx.page.locator('.quick-input-widget input');
  await input.waitFor({ timeout: TIMEOUTS.widgetMs });
  const row = ctx.page.locator('.quick-input-list .monaco-list-row', { hasText: name }).first();
  // A file written a moment ago reaches the search index late, and the list never re-asks by itself.
  for (let i = 0; i < 20 && !(await row.isVisible()); i++) {
    await input.fill('');
    await input.fill(name);
    await settle(400);
  }
  await ctx.page.keyboard.press('Enter');
  await ctx.page.locator('.tab.active', { hasText: name }).first().waitFor({ timeout: TIMEOUTS.widgetMs });
};

// No terminal left, so the next command opens a fresh one and its shot carries only its own run.
export const freshTerminal = async (ctx) => {
  await ctx.host.runCommand('Terminal: Kill All Terminals');
  await ctx.page.locator('.terminal-wrapper').first().waitFor({ state: 'detached', timeout: TIMEOUTS.terminalMs }).catch(() => {});
};

// The terminal printed `pattern` and then fell quiet.
export const printed = async (ctx, pattern) => {
  await ctx.page.waitForFunction((source) => new RegExp(source).test(
    document.querySelector('.terminal-wrapper.active .xterm-rows')?.innerText ?? ''), pattern.source, { timeout: 60_000 });
  await waitForStable(ctx.page, terminalText, { idleMs: 500, timeoutMs: 20_000 });
};

// A command line the shell echoed early, before its prompt came back, shows twice: bare at the end
// of the run before it, then after `$ `. Rows are joined first, so a wrapped command still matches.
const echoedTwice = () => {
  const runs = (document.querySelector('.terminal-wrapper.active .xterm-rows')?.innerText ?? '')
    .replace(/\n/g, '').split('$ ');
  return runs.some((run, i) => i > 1 && run.length > 12 && runs[i - 1].includes(run.slice(0, 40)));
};

// The extension types `cd` and its command as two lines, so the second can show up twice, and
// always does in a terminal just opened: the run is repeated in the cleared terminal until it reads once.
export const cleanRun = async (ctx, trigger, pattern, tries = 4) => {
  await freshTerminal(ctx);
  for (let i = 0; i < tries; i += 1) {
    if (i) {
      await ctx.page.locator('.terminal-wrapper.active .xterm').first().click();
      await ctx.page.keyboard.type('clear');
      await ctx.page.keyboard.press('Enter');
      await waitForStable(ctx.page, terminalText, { idleMs: 400, timeoutMs: 10_000 });
    }
    await trigger();
    await printed(ctx, pattern);
    if (i && !(await ctx.page.evaluate(echoedTwice))) return;
  }
};

// Screenshots for exactly as long as `drive` runs (lib/shot/shots.mjs `film` runs for a set time), so a
// clip ends on its last keystroke, never before it; answers the frame rate achieved.
export const filmWhile = async (ctx, framesDir, everyMs, shotOpts, drive) => {
  let done = false;
  let frames = 0;
  const startedAt = Date.now();
  const loop = (async () => {
    while (!done) {
      frames += 1;
      await ctx.page.screenshot({ path: path.join(framesDir, `frame-${String(frames).padStart(4, '0')}.png`), ...shotOpts });
      await settle(everyMs);
    }
  })();
  try { await drive(); } finally { done = true; await loop; }
  return { fps: Math.max(1, Math.round((frames * 1000) / (Date.now() - startedAt))) };
};

// The squiggles a revert leaves behind are re-checked off a debounce: wait them out.
export const noSquiggles = (ctx) => ctx.page.locator('.squiggly-error').first()
  .waitFor({ state: 'detached', timeout: TIMEOUTS.widgetMs }).catch(() => {});
