// The side panel's shots: the list docked beside the demo site, the four drop zones a row dragged
// out of it arms on the page (through the background, as a real drag does), and the crop page in
// a tab of its own — what Crop opens where the page refuses the in-page window.
import { applyShellTheme } from '../lib/theme/page.mjs';
import { waitForAnimations } from '../lib/waits.mjs';
import { MIN_ROWS, TIMEOUTS, VIEWS, context, freezeMotion, host, runner } from './session.mjs';
import { dockBesideSite, popup, surface } from './popup.mjs';

// Fractions of the viewport: zones.js splits it in four — here / incognito on top, new tab / crop
// below — so this point lights "Open in editor here".
const HERE = Object.freeze([0.25, 0.3]);

const panelPage = async (ctx, theme) => {
  if (!ctx.panel || ctx.panel.isClosed()) {
    ctx.panel = await surface('src/sidepanel/sidepanel.html', VIEWS.panel, theme);
    await ctx.panel.waitForSelector('.filters', { timeout: TIMEOUTS.scanMs });
    await dockBesideSite(ctx.panel);
  }
  await applyShellTheme(ctx.panel, theme);
  await ctx.panel.bringToFront();
  await ctx.panel.evaluate(() => document.getElementById('rescan').click());
  await ctx.panel.waitForFunction((min) => document.querySelectorAll('.row').length > min
    && !document.getElementById('status').textContent, MIN_ROWS, { timeout: TIMEOUTS.scanMs });
  await waitForAnimations(ctx.panel);
  return ctx.panel;
};

const rowEvent = (panel, name, type) => panel.evaluate(([label, kind]) => {
  const row = [...document.querySelectorAll('.row')].find((r) => r.querySelector('.name')?.textContent === label);
  const box = row.getBoundingClientRect();
  row.dispatchEvent(new DragEvent(kind, { bubbles: true, cancelable: true, dataTransfer: new DataTransfer(),
    clientX: box.left + 40, clientY: box.top + box.height / 2 }));
}, [name, type]);

export const makePanelSteps = ({ image }) => Object.freeze([
  { name: 'side-panel', run: async (ctx, theme) => {
    const panel = await panelPage(ctx, theme);
    await runner.shot(panel, 'side-panel');
  } },
  { name: 'site-dropzones', run: async (ctx, theme) => {
    const panel = await panelPage(ctx, theme);
    await rowEvent(panel, image, 'dragstart');
    await host.bringToFront();
    await host.locator('#stencil-ext-dropzones').waitFor({ state: 'attached', timeout: TIMEOUTS.scanMs });
    const src = await host.evaluate(() => document.querySelector('img[alt="Plate 2"]').src);
    await host.evaluate(([at, url]) => {
      const dt = new DataTransfer();
      dt.setData('text/uri-list', url);
      window.dispatchEvent(new DragEvent('dragover', { bubbles: true, cancelable: true, dataTransfer: dt,
        clientX: Math.round(innerWidth * at[0]), clientY: Math.round(innerHeight * at[1]) }));
    }, [HERE, src]);
    await host.waitForFunction(() => {
      const el = document.getElementById('stencil-ext-dropzones');
      return !!el && el.style.opacity === '1' && !!el.shadowRoot?.querySelector('.over');
    }, null, { timeout: TIMEOUTS.scanMs });
    await runner.shot(host, 'site-dropzones');
    await rowEvent(panel, image, 'dragend');
    await host.locator('#stencil-ext-dropzones').waitFor({ state: 'detached', timeout: TIMEOUTS.scanMs }).catch(() => {});
    await panel.close();
  } },
  // launchCrop with no tab is the extension's own fallback route: the crop page, keyed by ?k=.
  { name: 'crop-page', run: async (ctx, theme) => {
    const ui = await popup(ctx, theme);
    const src = await host.evaluate(() => document.querySelector('img[alt="Plate 2"]').src);
    const [crop] = await Promise.all([context.waitForEvent('page'), ui.evaluate(async ([url, resource]) => {
      const { launchCrop } = await import('/src/lib/stencil.js');
      await launchCrop({ src: url, source: url, resource });
    }, [src, host.url()])]);
    await crop.setViewportSize(VIEWS.crop);
    await crop.waitForFunction(() => document.getElementById('image')?.naturalWidth > 0, null, { timeout: TIMEOUTS.modalMs });
    await freezeMotion(crop);
    await applyShellTheme(crop, theme);
    await waitForAnimations(crop);
    await runner.shot(crop, 'crop-page');
    await crop.close();
  } },
]);
