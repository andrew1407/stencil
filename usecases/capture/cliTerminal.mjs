// Photographs the CLI's full-screen console in a real terminal: VS Code's integrated
// terminal (xterm.js), driven over the debug port and cropped to the terminal itself — the
// editor chrome and the panel's tab bar stay out of the picture. Output: usecases/cli/img/term-*.
//   node usecases/capture/cliTerminal.mjs [--only <name>]
import path from 'node:path';
import { loadCaptureConfig } from './lib/captureConfig.mjs';
import { outDir, scratchDir } from './lib/paths.mjs';
import { makeShotRunner } from './lib/shotRunner.mjs';
import { CLI_BIN, VsCodeHost } from './lib/vscodeHost.mjs';
import { film } from './lib/shots.mjs';
import { framesToGif, quantizePng } from './lib/gifTools.mjs';
import { settle, waitForStable } from './lib/waits.mjs';

const config = loadCaptureConfig('cli');
const runner = makeShotRunner({ config, out: outDir('cli') });
const TERMINAL = config.get('terminal');
const TIMEOUTS = config.get('timeouts');
const SCREEN = '.terminal-wrapper.active .xterm';

const host = await VsCodeHost.launch(config, runner.themeOf('term'), { open: null });
const { page } = host;
await host.runCommand('View: Toggle Primary Side Bar Visibility');
await page.keyboard.press('Control+`');
await page.locator(`${SCREEN}-screen`).first().waitFor({ timeout: 20_000 });
await host.runCommand('View: Toggle Maximized Panel');
const screen = page.locator(SCREEN).first();
await screen.click();

// The console redraws while it reveals output: "done" is a buffer that stopped changing.
const idle = (timeoutMs = TERMINAL.maxWaitMs) => waitForStable(
  page, () => document.querySelector('.terminal-wrapper.active .xterm-rows')?.innerText ?? '',
  { idleMs: TERMINAL.idleMs, timeoutMs },
);
const type = async (text, timeoutMs) => {
  await page.keyboard.type(text, { delay: 12 });
  await page.keyboard.press('Enter');
  await idle(timeoutMs);
};
// The console answers a network upload without touching the screen first: wait for the text
// that says it landed, not for the buffer to go quiet.
const waitForText = (pattern, timeoutMs) => page.waitForFunction(
  (source) => new RegExp(source).test(
    document.querySelector('.terminal-wrapper.active .xterm-rows')?.innerText ?? ''),
  pattern.source, { timeout: timeoutMs },
);
const shot = async (name) => quantizePng(await runner.shot(screen, name));
const screenClip = async () => ({ clip: await screen.boundingBox() });
// Terminal cell → page point, from the row stack xterm renders into.
const cellPoint = async (row, col) => {
  const box = await screen.boundingBox();
  const cell = await page.evaluate(() => {
    const rows = document.querySelectorAll('.terminal-wrapper.active .xterm-rows > div');
    const rect = rows[0].getBoundingClientRect();
    const cols = Number(document.querySelector('.terminal-wrapper.active .xterm')?.dataset.cols)
      || Math.round(rect.width / 8.4);
    return { w: rect.width / cols, h: rect.height };
  });
  return { x: box.x + (col - 0.5) * cell.w, y: box.y + (row - 0.5) * cell.h };
};
const clipGif = async (name, drive) => {
  const clip = config.get(`clips.${name}`);
  const frames = scratchDir(`${name}-frames`);
  const filming = film(page, frames, clip.ms, clip.everyMs, await screenClip());
  await drive(clip);
  const shot = await filming;
  framesToGif(frames, path.join(runner.out, `${name}.gif`), { ...config.gifLook, ...clip, inFps: shot.fps });
  console.log(`  ${name}.gif`);
};

// The server-backed assistant rides the CLI's env; the export line is typed before `clear`
// and the console's alternate screen hides the shell's scrollback anyway.
const token = config.serverToken();
if (token) {
  await type(`export STENCIL_LLM_PROVIDER=stencil-server STENCIL_LLM_SERVER_URL=${config.serverUrl}`
    + ` STENCIL_LLM_SERVER_TOKEN=${token}`);
}
await type(`clear; ${CLI_BIN(config)} --console-full-screen out/console.png`);
// `clear` empties the buffer, which reads as "idle" long before the console has drawn:
// wait for its own intro line instead.
await waitForText(/Console mode/, TERMINAL.maxWaitMs);
await idle();

// A URL upload asks for confirmation on a tty; 'y' answers it.
const load = async (ctx) => {
  if (ctx.loaded) return;
  await type(`/upload ${config.url('botIcon')}`);
  await page.keyboard.press('y');
  await waitForText(/image:\s+http/, TIMEOUTS.uploadMs);
  await idle(TIMEOUTS.uploadMs);
  ctx.loaded = true;
};

const STEPS = Object.freeze([
  { name: 'term-idle', run: () => shot('term-idle') },
  { name: 'term-help', run: async () => { await type('/help'); await shot('term-help'); } },
  { name: 'term-upload', run: async (ctx) => {
    await load(ctx);
    await type('/status');
    await shot('term-upload');
  } },
  { name: 'term-prompt', run: async (ctx) => {
    if (!token) { console.log('  term-prompt skipped (no server token)'); return; }
    await load(ctx);
    await type(`/prompt ${config.prompt('real')}`, TIMEOUTS.promptMs);
    await shot('term-prompt');
  } },
  { name: 'term-edit', run: async (ctx) => {
    await load(ctx);
    await type('/crop x1=10% x2=90% y1=10% y2=90%');
    await type('/rotate 1');
    await type('/filter bw');
    await type('/undo');
    await type('/save out/edited.png');
    await shot('term-edit');
  } },
  { name: 'term-blank', run: async () => {
    await type('/blank a5 black');
    await type('/script @rect (10%, 10%) (90%, 90%) ; @filter sepia');
    await shot('term-blank');
  } },
  // The header logo cycles the accent: two clicks, filmed.
  { name: 'term-theme-cycle', run: async () => {
    await type('/help');
    await clipGif('term-theme-cycle', async (clip) => {
      const point = await cellPoint(TERMINAL.logoCell.row, TERMINAL.logoCell.col);
      await page.mouse.click(point.x, point.y);
      await settle(clip.gapMs);
      await page.mouse.click(point.x, point.y);
    });
  } },
  { name: 'term-reveal', run: () => clipGif('term-reveal', async () => {
    await page.keyboard.type('/help', { delay: 12 });
    await page.keyboard.press('Enter');
  }) },
]);

console.log('cli console');
await runner.play(STEPS, { loaded: false });
await type('/exit');
await host.stop();
runner.finish();
