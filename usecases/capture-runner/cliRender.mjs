// Renders the HTML terminals cli_listings.py wrote (.out/cli/*.html) into
// usecases/docs/cli/img/*.png. Run after it:
//   node usecases/capture-runner/cliRender.mjs [--only <name>]
import fs from 'node:fs';
import path from 'node:path';
import { pathToFileURL } from 'node:url';
import { chromium } from './lib/playwright.mjs';
import { loadCaptureConfig } from './lib/captureConfig.mjs';
import { outDir, scratchPath } from './lib/paths.mjs';
import { makeShotRunner } from './lib/shot/runner.mjs';
import { quantizePng } from './lib/gifTools.mjs';

const config = loadCaptureConfig('cli');
const runner = makeShotRunner({ config, out: outDir('cli') });
const SOURCE = scratchPath('cli');
const { width, height, scaleFactor } = config.get('page');

const browser = await chromium.launch();
// A golden may set its own `scaleFactor`: the long --help listing fits the image budget below 2x.
const pages = new Map();
const pageAt = async (scale) => {
  if (!pages.has(scale)) pages.set(scale, await browser.newPage({ viewport: { width, height }, deviceScaleFactor: scale }));
  return pages.get(scale);
};

// Every terminal sits on a transparent page; the shot is the framed .term box alone.
const files = fs.readdirSync(SOURCE).filter((name) => name.endsWith('.html')).sort();
const STEPS = Object.freeze(files.map((file) => ({
  name: file.replace(/\.html$/, ''),
  run: async (ctx, theme, name) => {
    const page = await pageAt(config.get(`goldens.${name}.scaleFactor`, scaleFactor));
    await page.goto(pathToFileURL(path.join(SOURCE, file)).href);
    quantizePng(await runner.shot(page.locator('.term'), name, { omitBackground: true }));
  },
})));

console.log('cli listings');
await runner.play(STEPS);
await browser.close();
runner.finish();
