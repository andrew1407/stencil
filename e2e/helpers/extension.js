// Shared launch boilerplate for the extension suites: load the unpacked MV3 extension
// in a persistent Chromium context (Playwright's default `page` fixture can't load
// extensions, so each suite manages its own) and resolve the background service worker
// + the extension id. Launches headed — extensions load most reliably that way; CI
// wraps the job in xvfb (see ci.yml / README). Storage seeding (editorUrl,
// exposeWindowStencil, llmSettings, …) stays with each suite: it is per-suite contract.
import { chromium } from '@playwright/test';
import { fileURLToPath } from 'node:url';
import path from 'node:path';

export const EXT_PATH = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../browser-extension');

// Returns { context, background, extId } — `background()` resolves the MV3 service
// worker (waiting for it if it is still starting up).
export async function launchExtension() {
  const context = await chromium.launchPersistentContext('', {
    headless: false, // extensions load most reliably headed; CI runs under xvfb
    channel: 'chromium',
    args: [`--disable-extensions-except=${EXT_PATH}`, `--load-extension=${EXT_PATH}`],
  });
  const background = async () => {
    let [sw] = context.serviceWorkers();
    if (!sw) sw = await context.waitForEvent('serviceworker', { timeout: 15_000 });
    return sw;
  };
  const extId = new URL((await background()).url()).host;
  return { context, background, extId };
}
