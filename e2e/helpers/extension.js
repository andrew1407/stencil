// Shared launch boilerplate for the extension suites: load the unpacked MV3 extension in a
// persistent Chromium context and resolve the background service worker + the extension id.
// Storage seeding stays with each suite: it is per-suite contract.
import { chromium } from '@playwright/test';
import { fileURLToPath } from 'node:url';
import path from 'node:path';

export const EXT_PATH = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../browser-extension');

// `background()` resolves the MV3 service worker, waiting for it while it starts.
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

// The in-page modal mounts its shell in a CLOSED shadow root (the host page must not reach the
// iframe), so a spec reads it through the DevTools protocol, whose pierce walk sees closed roots:
// `fn` runs with `this` bound to the shadow root of the element whose id is `hostId`.
export async function inClosedShadow(page, hostId, fn) {
  const cdp = await page.context().newCDPSession(page);
  try {
    const { root } = await cdp.send('DOM.getDocument', { depth: -1, pierce: true });
    const find = (n) => {
      const attrs = n.attributes || [];
      for (let i = 0; i < attrs.length; i += 2) if (attrs[i] === 'id' && attrs[i + 1] === hostId) return n;
      for (const c of [...(n.children || []), ...(n.shadowRoots || [])]) {
        const hit = find(c);
        if (hit) return hit;
      }
      return null;
    };
    const host = find(root);
    if (!host?.shadowRoots?.length) return null;
    const { object } = await cdp.send('DOM.resolveNode', { backendNodeId: host.shadowRoots[0].backendNodeId });
    const { result, exceptionDetails } = await cdp.send('Runtime.callFunctionOn', {
      objectId: object.objectId, functionDeclaration: fn.toString(), returnByValue: true,
    });
    if (exceptionDetails) throw new Error(exceptionDetails.exception?.description || exceptionDetails.text);
    return result.value;
  } finally {
    await cdp.detach();
  }
}
