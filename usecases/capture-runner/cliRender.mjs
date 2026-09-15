// Renders the HTML terminals cli_listings.py wrote (.out/cli/*.html) into
// usecases/docs/cli/img/*.png. Run after it:
//   node usecases/capture-runner/cliRender.mjs [--only <name>]
import fs from 'node:fs';
import path from 'node:path';
import { pathToFileURL } from 'node:url';
import { chromium } from './lib/playwright.mjs';
import { loadCaptureConfig } from './lib/captureConfig.mjs';
import { outDir, scratchPath } from './lib/paths.mjs';
import { makeShotRunner } from './lib/shotRunner.mjs';
import { quantizePng } from './lib/gifTools.mjs';

const config = loadCaptureConfig('cli');
const runner = makeShotRunner({ config, out: outDir('cli') });
const SOURCE = scratchPath('cli');
const { width, height, scaleFactor } = config.get('page');

const browser = await chromium.launch();
const page = await browser.newPage({ viewport: { width, height }, deviceScaleFactor: scaleFactor });

// Every terminal sits on a transparent page; the shot is the framed .term box alone.
const pages = fs.readdirSync(SOURCE).filter((name) => name.endsWith('.html')).sort();
const STEPS = Object.freeze(pages.map((file) => ({
  name: file.replace(/\.html$/, ''),
  run: async (ctx, theme, name) => {
    await page.goto(pathToFileURL(path.join(SOURCE, file)).href);
    quantizePng(await runner.shot(page.locator('.term'), name, { omitBackground: true }));
  },
})));

console.log('cli listings');
await runner.play(STEPS);
await browser.close();
runner.finish();
