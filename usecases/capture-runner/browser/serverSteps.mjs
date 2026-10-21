// The collaboration shots under usecases/docs/browser/img: the Servers window with a live
// connection, a server project in the Projects list, and two people editing one project at once.
// They connect only to the collaboration server lib/collab.mjs hands over (config server.url).
import { recordPairGif } from '../lib/shot/shots.mjs';
import { settle, waitForAnimations } from '../lib/waits.mjs';

export function makeServerSteps({ config, runner, pages, collab, browser }) {
  const { fresh, openModal, closeModal, gotoApp, seedProjects } = pages;
  const loadMs = config.get('timeouts.loadMs');
  const look = config.get('clips.coedit');

  const connect = async (page) => {
    const token = await collab.mint();
    await page.evaluate(async ([url, key]) => { await window.stencil.connect({ url, token: key }); }, [collab.url, token]);
  };
  // The picture loaded into a project that lives on the server; resolves its server id.
  const shareOnServer = (page, name) => page.evaluate(async ([pic, url, label]) => {
    const before = new Set((await window.stencil.serverProjects()).map((p) => p.id));
    await window.stencil.load(pic, { address: url, name: label });
    for (let i = 0; i < 100; i += 1) {
      const fresh = (await window.stencil.serverProjects()).find((p) => !before.has(p.id));
      if (fresh) return fresh.id;
      await new Promise((r) => setTimeout(r, 150));
    }
    throw new Error('the server project never appeared');
  }, [config.url('botIcon'), collab.url, name]);

  // Folded controls: in a side-by-side clip the pictures are the subject.
  const coeditPage = async (page, theme) => {
    await page.emulateMedia({ colorScheme: theme });
    await gotoApp(page);
    await page.evaluate((dark) => { window.stencil.darkTheme = dark; }, theme === 'dark');
    await connect(page);
  };
  const fold = (page) => page.locator('#toggle-controls').click();
  const lineCount = (page) => page.evaluate(() => window.stencil.lines.length);
  const waitForLines = (page, n) => page.waitForFunction((want) => window.stencil.lines.length >= want, n, { timeout: loadMs });
  // A new line is selected; the bar's own button lets it go, so both pictures read clean.
  const deselect = async (page) => {
    const button = page.locator('#sel-deselect');
    if (await button.isVisible()) await button.click();
  };

  return Object.freeze([
    { name: 'connect-modal', run: async (ctx, theme) => {
      const page = await fresh(theme);
      await connect(page);
      await openModal(page, 'servers', 'connect-modal-overlay');
      await page.locator('.connect-row').first().waitFor({ timeout: loadMs });
      await waitForAnimations(page, '#connect-modal-overlay');
      await runner.shot(page, 'connect-modal');
      await closeModal(page, 'connect-modal-overlay');
      await page.close();
    } },
    { name: 'projects-server', run: async (ctx, theme) => {
      const page = await fresh(theme);
      await connect(page);
      await seedProjects(page);
      await page.evaluate(() => window.stencil.newEditor());
      await shareOnServer(page, 'Shared poster');
      // Back on a local project, so the server one shows its own gold outline, not the open one's.
      await page.evaluate(() => window.stencil.getProjectByName('Kitchen plan').open());
      await page.waitForFunction(() => window.stencil.current?.name === 'Kitchen plan', null, { timeout: loadMs });
      await openModal(page, 'projects', 'projects-modal-overlay');
      await page.locator('.project-row.project-remote').first().waitFor({ timeout: loadMs });
      await waitForAnimations(page, '#projects-modal-overlay');
      await runner.shot(page, 'projects-server');
      await closeModal(page, 'projects-modal-overlay');
      await page.close();
    } },
    // Left draws a line, right adds a rectangle; each edit appears on the other side by itself.
    { name: 'coedit', run: (ctx, theme) => recordPairGif(browser, runner.out, 'coedit', async (left, right, mark) => {
      await coeditPage(left, theme);
      await coeditPage(right, theme);
      await shareOnServer(left, 'Shared poster');
      // The right-hand person opens it from their own Projects list, as a person does.
      await openModal(right, 'projects', 'projects-modal-overlay');
      await right.locator('.project-row.project-remote', { hasText: 'Shared poster' }).first().click({ timeout: loadMs });
      await right.locator('#confirm-modal-confirm').click({ timeout: loadMs });
      await right.waitForFunction(() => window.stencil?.imageSize?.width > 0, null, { timeout: loadMs });
      // No coordinate tip over the pictures: they are what this clip is about.
      // The fit waits for the fold to finish, or it measures the canvas before it has grown.
      for (const page of [left, right]) {
        await fold(page);
        await waitForAnimations(page);
        await page.evaluate(() => { window.stencil.apply({ showTooltip: false, lineColor: '#e11d48', thickness: 5 }); window.stencil.zoomFit(); });
        await waitForAnimations(page);
      }
      await settle(look.leadMs);
      await mark();
      const box = await left.locator('#canvas').boundingBox();
      await left.evaluate(() => window.stencil.startDrawing());
      for (const [fx, fy] of look.line) {
        await left.mouse.click(box.x + box.width * fx, box.y + box.height * fy);
        await settle(look.stepMs);
      }
      await left.evaluate(() => window.stencil.stopDrawing());
      await deselect(left);
      await waitForLines(right, await lineCount(left));
      await settle(look.stepMs);
      const rbox = await right.locator('#canvas').boundingBox();
      await right.evaluate(() => { window.stencil.apply({ drawMode: 'rect', lineColor: '#2563eb' }); });
      const [[x1, y1], [x2, y2]] = look.rect.map(([fx, fy]) => [rbox.x + rbox.width * fx, rbox.y + rbox.height * fy]);
      await right.mouse.move(x1, y1);
      await right.mouse.down();
      await right.mouse.move(x2, y2, { steps: look.dragSteps });
      await right.mouse.up();
      await deselect(right);
      await waitForLines(left, await lineCount(right));
      await settle(look.holdMs);
    }, { ...config.gifLook, ...look }) },
  ]);
}
