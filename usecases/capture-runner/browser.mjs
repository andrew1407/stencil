// Captures usecases/docs/browser/img/*.png|gif from the served browser app, driven through
// window.stencil exactly like e2e/tests/browser/ui-pins.spec.js. The shots live in
// browser/, the constants in config/browser.json.
//   node usecases/capture-runner/browser.mjs [--only <name>]
import { chromium } from './lib/playwright.mjs';
import { loadCaptureConfig } from './lib/captureConfig.mjs';
import { outDir } from './lib/paths.mjs';
import { startAppServer, startMediaServer } from './lib/servers.mjs';
import { startLlmStub, startOllamaFace } from './lib/llmStub.mjs';
import { startCollab } from './lib/collab.mjs';
import { makeShotRunner } from './lib/shot/runner.mjs';
import { clearToasts, makeBrowserPages } from './browser/pageTools.mjs';
import { makeStillSteps } from './browser/stillSteps.mjs';
import { makeHandoffSteps } from './browser/handoffSteps.mjs';
import { makeEditSteps } from './browser/editSteps.mjs';
import { makeConsoleSteps } from './browser/consoleSteps.mjs';
import { makeDragSteps } from './browser/dragSteps.mjs';
import { makeServerSteps } from './browser/serverSteps.mjs';
import { makeClipSteps } from './browser/clipSteps.mjs';
import { makeGestureSteps } from './browser/gestureSteps.mjs';
import { makeVideoSteps } from './browser/videoSteps.mjs';
import { sampleVideo } from './lib/sampleMedia.mjs';

const config = loadCaptureConfig('browser');
const runner = makeShotRunner({ config, out: outDir('browser'), quantize: true, beforeShot: clearToasts });
const server = await startAppServer();
const clip = sampleVideo();
const media = await startMediaServer(clip.dir, config.get('mediaPort'));
const stub = await startLlmStub();
const ollama = await startOllamaFace(config.get('ollama.port'), config.get('ollama.version'));
const collab = await startCollab(config);
const browser = await chromium.launch();
const pages = makeBrowserPages({ config, browser });
// A pass's pages close with it: the shared one and the seeded Projects one.
const closePass = async (ctx) => { await ctx.page?.close(); await ctx.projects?.close(); };

console.log('browser stills');
const ctx = await runner.play(makeStillSteps({ config, runner, pages, stub, ollama, appUrl: server.url, browser }), {});
await runner.play(makeHandoffSteps({ config, runner, pages }), ctx);
await runner.play(makeEditSteps({ config, runner, pages }), ctx);
await runner.play(makeConsoleSteps({ runner, pages }), ctx);
await closePass(ctx);

// Its own page: a clip left in the shared one would sit under every later shot.
console.log('browser video');
const videoCtx = await runner.play(makeVideoSteps({
  config, runner, pages, clipPath: clip.file, clipUrl: media.url(clip.name),
}), {});
await closePass(videoCtx);

// Its own pass: a drag is held open across the shot, so it must not share a page with a still.
console.log('browser drag');
await closePass(await runner.play(makeDragSteps({ config, runner, pages }), {}));

console.log(`browser servers (${collab.url})`);
await runner.play(makeServerSteps({ config, runner, pages, collab, browser }));

console.log('browser clips');
await runner.play(makeClipSteps({
  config, runner, browser, pages: { ...pages, stubUrl: stub.url, queuePlan: (plan) => stub.queue(plan) },
}));

console.log('browser gestures');
await runner.play(makeGestureSteps({ config, runner, browser, pages }));

await browser.close();
await stub.close();
ollama.stop();
await collab.stop();
media.stop();
server.stop();
await runner.finish();
