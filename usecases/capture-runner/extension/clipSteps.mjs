// The side panel's two clips, filmed beside the demo site and played side by side: a sweep down
// the list with highlight on hover, each row outlining its element and scrolling the page to it,
// and a row dragged out onto the page's drop zones and let go on Crop, which opens the crop window.
import path from 'node:path';
import { pairToGif } from '../lib/gifTools.mjs';
import { scratchPath } from '../lib/paths.mjs';
import { startCast } from '../lib/shot/cast.mjs';
import { pointerOnPage } from '../lib/shot/pointer.mjs';
import { settle, waitForAnimations, waitForStable } from '../lib/waits.mjs';
import { TIMEOUTS, VIEWS, config, host, runner } from './session.mjs';
import { foldFilters, rowNamed } from './popup.mjs';
import { panelPage, rowEvent } from './panelSteps.mjs';
import { CROP_PAGE, modalFrame, reloadSite } from './siteSteps.mjs';

// Both pages at one height, so the two halves meet in one frame.
const sizeTo = async (panel, height) => {
  await panel.setViewportSize({ width: VIEWS.panel.width, height });
  await host.setViewportSize({ width: VIEWS.site.width, height });
  await host.evaluate(() => scrollTo(0, 0));
};

// The site plays on the left and the panel on the right, where Chrome docks a side panel.
const filmPair = async (name, panel, act) => {
  const casts = [await startCast(host, scratchPath('clips', `${name}-site`)), await startCast(panel, scratchPath('clips', `${name}-panel`))];
  await act();
  const [left, right] = await Promise.all(casts.map((cast) => cast.stop()));
  // A tab behind another streams its whole window, the emulated viewport in its top-left corner.
  const dpr = config.get('dpr');
  const cropPx = [host, panel].map((page) => ({ x: 0, y: 0, width: page.viewportSize().width * dpr, height: page.viewportSize().height * dpr }));
  pairToGif(left, right, path.join(runner.out, `${name}.gif`), { ...config.gifLook, width: config.get(`clips.${name}.width`), gap: 8, cropPx });
  console.log(`  ${name}.gif`);
};

const namePoint = async (panel, name) => {
  const label = rowNamed(panel, name).locator('.name');
  await label.scrollIntoViewIfNeeded();
  const box = await label.boundingBox();
  return [box.x + box.width / 2, box.y + box.height / 2];
};

// A real mouse, paced so the drawn pointer travels rather than jumps.
const travel = async (page, from, to, { steps, stepMs }) => {
  for (let i = 1; i <= steps; i += 1) {
    await page.mouse.move(from[0] + ((to[0] - from[0]) * i) / steps, from[1] + ((to[1] - from[1]) * i) / steps);
    await settle(stepMs);
  }
  return to;
};

// A drag's pointer is its dragover stream; `uri` is what the page's drop zones read off it.
const glide = (page, from, to, { steps, stepMs }, uri = '') => page.evaluate(async ([a, b, n, ms, url]) => {
  for (let i = 1; i <= n; i += 1) {
    const x = a[0] + ((b[0] - a[0]) * i) / n;
    const y = a[1] + ((b[1] - a[1]) * i) / n;
    const dataTransfer = new DataTransfer();
    if (url) dataTransfer.setData('text/uri-list', url);
    (document.elementFromPoint(x, y) || document.body).dispatchEvent(new DragEvent('dragover',
      { bubbles: true, cancelable: true, dataTransfer, clientX: x, clientY: y }));
    await new Promise((done) => setTimeout(done, ms));
  }
}, [from, to, steps, stepMs, uri]);

// Chrome draws a drag's image outside the page, so the pointer carries the one the row hands to
// setDragImage, photographed on a clear page before the clip starts. SHADOW px keep its shadow.
const SHADOW = 32;
const dragImage = async (panel, name, grip) => {
  await panel.evaluate(() => {
    const own = DataTransfer.prototype.setDragImage;
    DataTransfer.prototype.setDragImage = function keep(el, x, y) {
      window.__docsDrag = { copy: el.cloneNode(true), x, y };
      DataTransfer.prototype.setDragImage = own;
    };
  });
  await rowEvent(panel, name, 'dragstart', grip);
  await rowEvent(panel, name, 'dragend', grip);
  await host.locator('#stencil-ext-dropzones').waitFor({ state: 'detached', timeout: TIMEOUTS.scanMs });
  const box = await panel.evaluate((pad) => {
    const { copy } = window.__docsDrag;
    copy.id = 'docs-drag';
    Object.assign(copy.style, { position: 'fixed', top: `${pad}px`, left: `${pad}px` });
    const stage = document.createElement('style');
    stage.id = 'docs-drag-stage';
    stage.textContent = 'html,body{background:transparent!important}body>*:not(#docs-drag){visibility:hidden!important}';
    document.head.append(stage);
    document.body.append(copy);
    const { width, height } = copy.getBoundingClientRect();
    return { width: width + 2 * pad, height: height + 2 * pad };
  }, SHADOW);
  const png = await panel.screenshot({ omitBackground: true, animations: 'disabled', clip: { x: 0, y: 0, ...box } });
  const [x, y] = await panel.evaluate(() => {
    document.getElementById('docs-drag').remove();
    document.getElementById('docs-drag-stage').remove();
    return [window.__docsDrag.x, window.__docsDrag.y];
  });
  return [`data:image/png;base64,${png.toString('base64')}`, x + SHADOW, y + SHADOW, box.width];
};

const outlined = (name) => host.waitForFunction((file) => {
  const el = document.querySelector('[data-stencil-listhover]');
  return !!el && (el.currentSrc || el.src || '').endsWith(`/${file}`);
}, name, { timeout: TIMEOUTS.scanMs });

const leavePanel = async (ctx, panel, theme) => {
  await panel.evaluate(() => {
    for (const head of document.querySelectorAll('.fsection:not([id]).collapsed > .section-head')) head.click();
  });
  await panel.close();
  await reloadSite(ctx, theme);
};

export const makeClipSteps = ({ image }) => Object.freeze([
  { name: 'site-hover', run: async (ctx, theme) => {
    const look = config.get('clips.site-hover');
    const panel = await panelPage(ctx, theme);
    await sizeTo(panel, look.height);
    await foldFilters(panel);
    await panel.evaluate(() => document.getElementById('f-hover-hl').click());
    await pointerOnPage(panel);
    let at = [VIEWS.panel.width * look.start[0], look.height * look.start[1]];
    await panel.mouse.move(...at);
    await filmPair('site-hover', panel, async () => {
      await settle(look.leadMs);
      for (const name of look.rows) {
        at = await travel(panel, at, await namePoint(panel, name), look);
        await outlined(name);
        await waitForStable(host, () => scrollY, { idleMs: 300 });
        await settle(look.holdMs);
      }
    });
    await panel.evaluate(() => document.getElementById('f-hover-hl').click());
    await leavePanel(ctx, panel, theme);
  } },
  // The drop goes through the background, as a real one does: launchCrop over the page it hit.
  { name: 'site-drop-crop', run: async (ctx, theme) => {
    const look = config.get('clips.site-drop-crop');
    const panel = await panelPage(ctx, theme);
    await sizeTo(panel, look.height);
    await foldFilters(panel);
    await pointerOnPage(panel);
    await pointerOnPage(host);
    const grip = await namePoint(panel, image);
    await panel.mouse.move(...grip);
    const src = await host.evaluate(() => document.querySelector('img[alt="Plate 2"]').src);
    const carried = await dragImage(panel, image, grip);
    const route = look.route.map(([x, y]) => [VIEWS.site.width * x, look.height * y]);
    await filmPair('site-drop-crop', panel, async () => {
      await settle(look.leadMs);
      await panel.evaluate(([x, y]) => document.elementFromPoint(x, y).dispatchEvent(new PointerEvent('pointerdown',
        { bubbles: true, pointerType: 'mouse', clientX: x, clientY: y })), grip);
      await rowEvent(panel, image, 'dragstart', grip);
      for (const page of [panel, host]) await page.evaluate((args) => window.__docsPointer.carry(...args), carried);
      await host.locator('#stencil-ext-dropzones').waitFor({ state: 'attached', timeout: TIMEOUTS.scanMs });
      await glide(panel, grip, [VIEWS.panel.width - 2, grip[1]], look);
      await panel.evaluate(() => window.__docsPointer.hide());
      let from = [2, grip[1]];
      for (const to of route) {
        await glide(host, from, to, look, src);
        from = to;
      }
      await settle(look.aimMs);
      await host.evaluate(([[x, y], url]) => {
        const dataTransfer = new DataTransfer();
        dataTransfer.setData('text/uri-list', url);
        window.dispatchEvent(new DragEvent('drop', { bubbles: true, cancelable: true, dataTransfer, clientX: x, clientY: y }));
        window.__docsPointer.carry(null);
      }, [from, src]);
      await rowEvent(panel, image, 'dragend', grip);
      await waitForAnimations(await modalFrame(CROP_PAGE, () => document.getElementById('image')?.naturalWidth > 0, TIMEOUTS.modalMs));
      await waitForAnimations(host);
      await settle(look.holdMs);
    });
    await leavePanel(ctx, panel, theme);
  } },
]);
