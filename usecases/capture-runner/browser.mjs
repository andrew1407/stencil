// Captures usecases/docs/browser/img/*.png|gif from the served browser app, driven through
// window.stencil exactly like e2e/tests/browser/ui-pins.spec.js. The shots live in
// browser/, the constants in config/browser.json.
//   node usecases/capture-runner/browser.mjs [--only <name>]
import { chromium } from './lib/playwright.mjs';
import { loadCaptureConfig } from './lib/captureConfig.mjs';
import { outDir } from './lib/paths.mjs';
import { startAppServer, startMediaServer } from './lib/servers.mjs';
import { startLlmStub } from './lib/llmStub.mjs';
import { makeShotRunner } from './lib/shotRunner.mjs';
import { makeBrowserPages } from './browser/pageTools.mjs';
import { makeStillSteps } from './browser/stillSteps.mjs';
import { makeDragSteps } from './browser/dragSteps.mjs';
import { makeClipSteps } from './browser/clipSteps.mjs';
import { makeVideoSteps } from './browser/videoSteps.mjs';
import { sampleVideo } from './lib/sampleMedia.mjs';

const config = loadCaptureConfig('browser');
const runner = makeShotRunner({ config, out: outDir('browser') });
const server = await startAppServer();
const clip = sampleVideo();
const media = await startMediaServer(clip.dir, config.get('mediaPort'));
const stub = await startLlmStub();
const browser = await chromium.launch();
const pages = makeBrowserPages({ config, browser });

console.log('browser stills');
const ctx = await runner.play(makeStillSteps({ config, runner, pages, stub, appUrl: server.url, browser }), {});
await ctx.page?.close();

// Its own page: a clip left in the shared one would sit under every later shot.
console.log('browser video');
const videoCtx = await runner.play(makeVideoSteps({
  config, runner, pages, clipPath: clip.file, clipUrl: media.url(clip.name),
}), {});
await videoCtx.page?.close();

// Its own pass: a drag is held open across the shot, so it must not share a page with a still.
console.log('browser drag');
const dragCtx = await runner.play(makeDragSteps({ config, runner, pages }), {});
await dragCtx.page?.close();

console.log('browser clips');
await runner.play(makeClipSteps({
  config, runner, browser, pages: { ...pages, stubUrl: stub.url, queuePlan: (plan) => stub.queue(plan) },
}));

await browser.close();
await stub.close();
media.stop();
server.stop();
runner.finish();
