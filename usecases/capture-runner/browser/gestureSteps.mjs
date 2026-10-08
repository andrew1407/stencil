// The drag-gesture clips under usecases/docs/browser/img: a toolbar icon, the logo, the theme
// switch and a colour swatch dragged with the real mouse, so the app's own gesture machines run
// (js/ui/drag/). The video carries no cursor; the ghost, chip or lens the app draws is the pointer.
import { recordGif } from '../lib/shot/shots.mjs';
import { settle, waitForAnimations, waitForCanvasChange, canvasSize } from '../lib/waits.mjs';

const centre = (box) => ({ x: box.x + box.width / 2, y: box.y + box.height / 2 });

export function makeGestureSteps({ config, runner, browser, pages }) {
  const clips = config.get('clips');
  const loadMs = config.get('timeouts.loadMs');

  // A press on `from`, a glide through each point of `path`, a release; `look.stepMs` paces it.
  const drag = async (page, from, path, look) => {
    await page.mouse.move(from.x, from.y);
    await page.mouse.down();
    let at = from;
    for (const to of path) {
      for (let i = 1; i <= look.steps; i += 1) {
        await page.mouse.move(at.x + ((to.x - at.x) * i) / look.steps, at.y + ((to.y - at.y) * i) / look.steps);
        await settle(look.stepMs);
      }
      at = to;
      await settle(look.pauseMs ?? 0);
    }
    await page.mouse.up();
  };
  // Off the controls, so no tooltip is up before a press or after a drop.
  const rest = async (page) => page.mouse.move(...Object.values(inView(page, [0.5, 0.97])));
  const at = async (page, selector) => centre(await page.locator(selector).first().boundingBox());
  const inView = (page, [fx, fy]) => {
    const v = page.viewportSize();
    return { x: Math.round(v.width * fx), y: Math.round(v.height * fy) };
  };
  // A page with the shots' lines on it, or on one of the repo's pictures when `image` names one.
  const setup = async (page, look) => {
    await page.emulateMedia({ colorScheme: look.theme });
    await pages.gotoApp(page);
    await pages.blank(page);
    if (look.image) {
      const before = await canvasSize(page);
      await page.evaluate((url) => window.stencil.load(url), config.url(look.image));
      await waitForCanvasChange(page, before, loadMs);
    }
    await page.evaluate((lines) => window.stencil.setLines(lines, { history: false }), config.get('canvas.lines'));
    if (look.filter) await page.evaluate((f) => { window.stencil.filter = f; }, look.filter);
    await page.evaluate(() => window.stencil.zoomFit());
    await rest(page);
    await waitForAnimations(page);
    await settle(look.leadMs);
  };
  const clip = (name, drive) => ({
    name,
    run: () => recordGif(browser, runner.out, name, async (page, mark) => {
      const look = { ...clips[name], theme: runner.themeOf(name) };
      await setup(page, look);
      mark();
      await drive(page, look);
      await rest(page);
      await settle(look.holdMs);
    }, { ...config.gifLook, ...clips[name] }),
  });

  return Object.freeze([
    clip('drag-icon-window', async (page, look) => {
      await drag(page, await at(page, '#projects-btn'), [inView(page, look.dropAt)], look);
      await page.locator('#projects-modal-overlay.modal-open').waitFor();
    }),
    clip('drag-icon-zoom', async (page, look) => {
      const from = await at(page, '#zoom-in');
      await drag(page, from, look.glide.map(([dx, dy]) => ({ x: from.x + dx, y: from.y + dy })), look);
    }),
    clip('drag-icon-canvas', async (page, look) => {
      await drag(page, await at(page, '#rotate-right'), [inView(page, look.dropAt)], look);
    }),
    clip('drag-logo-clean', async (page, look) => {
      await drag(page, await at(page, '.app-logo'), look.path.map((p) => inView(page, p)), look);
    }),
    clip('theme-lens', async (page, look) => {
      await drag(page, await at(page, '#theme-toggle'), look.path.map((p) => inView(page, p)), look);
    }),
    clip('drag-color', async (page, look) => {
      await page.locator('#coord-tab-lines').click();
      const row = page.locator('#lines-list .lines-row .lines-swatch').first();
      await row.waitFor();
      await drag(page, await at(page, '#line-color'), [centre(await row.boundingBox())], look);
    }),
  ]);
}
