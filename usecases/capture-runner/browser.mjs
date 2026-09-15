// Captures usecases/docs/browser/img/*.png|gif from the served browser app, driven through
// window.stencil exactly like e2e/tests/browser/ui-pins.spec.js. The shots live in
// browser/, the constants in config/browser.json.
//   node usecases/capture-runner/browser.mjs [--only <name>]
import { chromium } from './lib/playwright.mjs';
import { loadCaptureConfig } from './lib/captureConfig.mjs';
import { outDir } from './lib/paths.mjs';
import { startAppServer } from './lib/servers.mjs';
import { startLlmStub } from './lib/llmStub.mjs';
import { makeShotRunner } from './lib/shotRunner.mjs';
import { makeBrowserPages } from './browser/pageTools.mjs';
import { makeStillSteps } from './browser/stillSteps.mjs';
import { makeClipSteps } from './browser/clipSteps.mjs';

const config = loadCaptureConfig('browser');
const runner = makeShotRunner({ config, out: outDir('browser') });
const server = await startAppServer();
const stub = await startLlmStub();
const browser = await chromium.launch();
const pages = makeBrowserPages({ config, browser });

console.log('browser stills');
const ctx = await runner.play(makeStillSteps({ config, runner, pages, stub, appUrl: server.url, browser }), {});
await ctx.page?.close();

console.log('browser clips');
await runner.play(makeClipSteps({
  config, runner, browser, pages: { ...pages, stubUrl: stub.url, queuePlan: (plan) => stub.queue(plan) },
}));

await browser.close();
await stub.close();
server.stop();
runner.finish();
