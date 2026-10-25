// The browser-extension capture's session: its config and shot runner, the app, demo-site, clip
// and LLM-stub servers, the unpacked extension launched as e2e's helper does but at 2x, the
// collaboration server, and the demo-site tab every popup scans.
import { chromium, e2e } from '../lib/playwright.mjs';
import { loadCaptureConfig } from '../lib/captureConfig.mjs';
import { outDir } from '../lib/paths.mjs';
import { startAppServer, startMediaServer, startSiteServer } from '../lib/servers.mjs';
import { startLlmStub } from '../lib/llmStub.mjs';
import { CLIP, sampleVideo } from '../lib/sampleMedia.mjs';
import { startCollab } from '../lib/collab.mjs';
import { makeShotRunner } from '../lib/shot/runner.mjs';

export const config = loadCaptureConfig('browserExtension');
// A passing toast in an editor tab is never what a still is about.
const clearToasts = async (target) => {
  if (target.mouse) await target.evaluate(() => document.querySelectorAll('.notify-toast').forEach((t) => t.remove()));
};
export const runner = makeShotRunner({ config, out: outDir('browser-extension'), quantize: true, beforeShot: clearToasts });
export const VIEWS = config.get('viewports');
export const TIMEOUTS = config.get('timeouts');
export const MIN_ROWS = config.get('minRows');

const { EXT_PATH, inClosedShadow } = await e2e('helpers/extension.js');
export { inClosedShadow };
export const { freezeMotion } = await e2e('helpers/uiPin.js');
export const { llmSettings } = await e2e('helpers/chat.js');

export const app = await startAppServer();
export const site = await startSiteServer(config.get('sitePort'));
// Its own file name, so a browser capture regenerating the shared sample never races this one.
const clip = sampleVideo({ ...CLIP, name: 'extension-clip.mp4' });
export const media = await startMediaServer(clip.dir, config.get('mediaPort'));
export const stub = await startLlmStub();
export const collab = await startCollab(config);

export const context = await chromium.launchPersistentContext('', {
  headless: false,
  channel: 'chromium',
  viewport: VIEWS.site,
  deviceScaleFactor: config.get('dpr'),
  args: [`--disable-extensions-except=${EXT_PATH}`, `--load-extension=${EXT_PATH}`],
});
export const background = async () => context.serviceWorkers()[0]
  ?? context.waitForEvent('serviceworker', { timeout: TIMEOUTS.scanMs });
export const extId = new URL((await background()).url()).host;
const sw = await background();
await sw.evaluate((editorUrl) => new Promise((done) => chrome.storage.sync.set({ editorUrl }, done)), app.url);

// The editor tabs the extension opens load with motion off, so a picture never lands mid-arrival.
const seed = await context.newPage();
await seed.goto(app.url);
await seed.evaluate(() => localStorage.setItem('drawingApp_motion', JSON.stringify({ mode: 'none' })));
await seed.close();

export const host = await context.newPage();
await host.setViewportSize(VIEWS.site);
await host.goto(`${site.url}?media=${encodeURIComponent(media.url(clip.name))}`);
await host.locator('figure img').first().waitFor();
// Parked on a real frame: the scan lists a paused clip at 0 s by its poster alone.
await host.waitForFunction(() => {
  const v = document.getElementById('clip');
  return !!v && v.readyState >= 2 && v.currentTime > 1 && !v.seeking;
}, null, { timeout: TIMEOUTS.scanMs });
