// The browser app as the capture drives it: a fresh page in a given theme, the shared page
// the long middle of the run reuses, and the canvas / modal gestures. Everything goes
// through window.stencil, the same facade the e2e specs use.
import { e2e } from '../lib/playwright.mjs';
import { applyAppTheme } from '../lib/theme/page.mjs';
import { waitForAnimations } from '../lib/waits.mjs';

export const { gotoApp, settleModalAnimations, expectModalOpen } = await e2e('helpers/boot.js');
export const { freezeMotion } = await e2e('helpers/uiPin.js');
export const { seedLlmSettings, sendChat } = await e2e('helpers/chat.js');
export const { finger } = await e2e('helpers/drag.js');

// A passing "Saved" toast is never what a still is about.
export const clearToasts = async (target) => {
  if (target.mouse) await target.evaluate(() => document.querySelectorAll('.notify-toast').forEach((t) => t.remove()));
};

export function makeBrowserPages({ config, browser }) {
  const view = config.get('viewports.app');
  const dpr = config.get('dpr');
  const blankSize = config.get('canvas.blankSize');

  const fresh = async (theme) => {
    const page = await browser.newPage({ viewport: view, deviceScaleFactor: dpr });
    await gotoApp(page);
    await freezeMotion(page);
    await applyAppTheme(page, theme);
    return page;
  };

  // One page for the whole still sequence; each step states the theme it needs.
  const shared = async (ctx, theme) => {
    ctx.page ??= await fresh(theme);
    await applyAppTheme(ctx.page, theme);
    return ctx.page;
  };

  const blank = (page, color = '#ffffff') =>
    page.evaluate(([fill, size]) => window.stencil.blank(fill, { size }), [color, blankSize]);

  // The repo picture at its full frame (no page-shape trim), so plan coordinates land as written;
  // load()'s `crop` is a pixel rectangle.
  const loadPicture = async (page, key = 'botIcon') => {
    const { width, height } = config.get('pictureSize');
    await page.evaluate(([url, rect]) => window.stencil.load(url, { crop: rect }), [config.url(key), { x: 0, y: 0, width, height }]);
    await page.waitForFunction(() => window.stencil.imageSize?.width > 0);
  };

  // The repo picture as a person opens it: trimmed to the page's shape.
  const loadTrimmed = async (page, key = 'botIcon') => {
    await page.evaluate((url) => window.stencil.load(url), config.url(key));
    await page.waitForFunction(() => window.stencil.imageSize?.width > 0);
  };

  // A Projects list worth reading: three named projects with distinct pictures, "Kitchen plan"
  // open. A project's thumbnail is rendered when the editor switches away from it, so the one
  // made last is left by re-opening the first.
  const seedProjects = (page) => page.evaluate(async ([pic, lines, size]) => {
    const s = window.stencil;
    const make = async (name, build) => {
      await s.newEditor();
      await build();
      s.current.name = name;
      await s.save();
    };
    await make('Kitchen plan', async () => {
      await s.blank('#ffffff', { size });
      s.setLines(lines, { history: false });
    });
    await make('Poster draft', async () => { await s.load(pic); s.filter = 'sepia'; });
    await make('Floor survey', async () => {
      await s.blank('#dde3ea', { size });
      s.setLines([{ points: [{ x: 160, y: 120 }, { x: 800, y: 120 }, { x: 800, y: 520 }, { x: 160, y: 520 }, { x: 160, y: 120 }],
        color: '#2563eb', thickness: 6 }], { history: false });
    });
    await s.getProjectByName('Kitchen plan').open();
  }, [config.url('botIcon'), config.get('canvas.lines'), blankSize])
    .then(() => page.waitForFunction(() => window.stencil.current?.name === 'Kitchen plan' && window.stencil.lines.length > 0));

  // One seeded page for every Projects shot of a pass.
  const projectsPage = async (ctx, theme) => {
    if (!ctx.projects) {
      ctx.projects = await fresh(theme);
      await seedProjects(ctx.projects);
    }
    await applyAppTheme(ctx.projects, theme);
    return ctx.projects;
  };

  const drawLines = async (page) => {
    await page.evaluate((lines) => window.stencil.setLines(lines, { history: false }), config.get('canvas.lines'));
    await page.locator('#coord-tab-lines').click();
    await page.locator('#lines-list .lines-row td').first().click();
    await page.locator('#selection-panel').waitFor();
    await waitForAnimations(page);
  };

  const openModal = async (page, key, overlay) => {
    await page.evaluate((k) => window.stencil.openWindow(k), key);
    await expectModalOpen(page, overlay);
    await settleModalAnimations(page, overlay);
  };

  const closeModal = async (page, overlay) => {
    await page.evaluate(() => window.stencil.closeWindow());
    await page.locator(`#${overlay}.modal-open`).waitFor({ state: 'detached', timeout: 5000 }).catch(() => {});
    await waitForAnimations(page);
  };

  // The stub's plan always mentions sepia; a real model's reply is awaited on the facade.
  const chatReplied = (page) => page.locator('#chat-transcript .chat-msg-assistant')
    .filter({ hasText: /sepia/ }).first().waitFor({ timeout: config.get('timeouts.stubReplyMs') });

  return { fresh, shared, blank, loadPicture, loadTrimmed, seedProjects, projectsPage, drawLines, openModal, closeModal, chatReplied,
    seedLlm: seedLlmSettings, send: sendChat, gotoApp, settleModalAnimations, expectModalOpen, finger };
}
