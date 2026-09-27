// The browser-extension capture's session: its config and shot runner, the app, demo-site and
// LLM-stub servers, the unpacked extension launched as e2e/tests/browser-extension/ui-pins.spec.js
// launches it, and the demo-site tab every popup scans.
import { e2e } from '../lib/playwright.mjs';
import { loadCaptureConfig } from '../lib/captureConfig.mjs';
import { outDir } from '../lib/paths.mjs';
import { startAppServer, startSiteServer } from '../lib/servers.mjs';
import { startLlmStub } from '../lib/llmStub.mjs';
import { makeShotRunner } from '../lib/shot/runner.mjs';

export const config = loadCaptureConfig('browserExtension');
export const runner = makeShotRunner({ config, out: outDir('browser-extension') });
export const VIEWS = config.get('viewports');
export const TIMEOUTS = config.get('timeouts');
export const MIN_ROWS = config.get('minRows');

const { launchExtension } = await e2e('helpers/extension.js');
export const { freezeMotion } = await e2e('helpers/uiPin.js');
export const { llmSettings } = await e2e('helpers/chat.js');

export const app = await startAppServer();
export const site = await startSiteServer(config.get('sitePort'));
export const stub = await startLlmStub();
export const { context, background, extId } = await launchExtension();
const sw = await background();
await sw.evaluate((editorUrl) => new Promise((done) => chrome.storage.sync.set({ editorUrl }, done)), app.url);

export const host = await context.newPage();
await host.setViewportSize(VIEWS.site);
await host.goto(site.url);
await host.locator('figure img').first().waitFor();
