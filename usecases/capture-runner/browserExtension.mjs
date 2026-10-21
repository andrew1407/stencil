// Captures usecases/docs/browser-extension/img/* from the unpacked MV3 extension at 2x, against the
// demo site in ./site, with a collaboration server for the connection and the server pins.
// Headed (extensions need it). Constants live in config/browserExtension.json.
//   node usecases/capture-runner/browserExtension.mjs [--only <name>]
import { collab, config, context, host, media, runner, site, stub, app } from './extension/session.mjs';
import { makeSiteSteps } from './extension/siteSteps.mjs';
import { makePopupSteps } from './extension/popupSteps.mjs';
import { makeServerSteps } from './extension/serverSteps.mjs';
import { makePanelSteps } from './extension/panelSteps.mjs';

const rows = config.get('rows');
const byName = Object.fromEntries([
  ...makeSiteSteps({ image: rows.image }),
  ...makePopupSteps({ image: rows.image, clipRow: rows.clip }),
  ...makeServerSteps({ serverPin: rows.serverPin, localPin: rows.localPin }),
  ...makePanelSteps({ image: rows.image }),
].map((step) => [step.name, step]));

// State builds up in this order: the image is opened before its row shows the opened flag, the
// server is connected before anything is pinned on it, and the options cards come after both.
const ORDER = [
  'site', 'popup-list-light', 'popup-list-dark', 'popup-row-menu', 'popup-video-menu',
  'site-highlight', 'site-crop-modal', 'site-editor-modal', 'editor-tab', 'editor-tab-incognito',
  'popup-opened', 'options-connect', 'popup-pin-dialog', 'popup-pinned', 'options', 'popup-assistant',
  'side-panel', 'site-dropzones', 'crop-page',
];

console.log('extension shots');
const ctx = await runner.play(ORDER.map((name) => byName[name]), {});
for (const page of [ctx.ui, ctx.panel]) if (page && !page.isClosed()) await page.close();

await host.close();
await context.close();
await stub.close();
await collab.stop();
media.stop();
site.stop();
app.stop();
await runner.finish();
