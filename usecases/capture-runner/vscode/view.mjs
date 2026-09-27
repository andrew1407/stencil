// The VS Code capture's frame: its config and shot runner, and the crops every shot is cut to —
// the editor group bounded by its rendered lines, the panel, the palette, the terminal down to its
// last written row — plus the caret and pointer-park helpers the steps share.
import { loadCaptureConfig } from '../lib/captureConfig.mjs';
import { outDir } from '../lib/paths.mjs';
import { makeShotRunner } from '../lib/shot/runner.mjs';
import { quantizePng } from '../lib/gifTools.mjs';

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
