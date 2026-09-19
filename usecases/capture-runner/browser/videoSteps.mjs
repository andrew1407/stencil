// The Open Image modal with a VIDEO in it: opened off disk, opened from a link, and the
// modal's own crop box drawn over the player. The clip comes from lib/sampleMedia.mjs, so
// nothing binary is committed. Desktop twin: capture-runner/desktop/captureUseCases.cpp.
import { waitForAnimations } from '../lib/waits.mjs';

const OVERLAY = 'open-image-modal-overlay';

// Predicates are passed as FUNCTIONS, never source strings: the app's CSP has no 'unsafe-eval'.
// Each tab owns its own img/video pair, so the live player is the DISPLAYED one.

// Ready = the player holds a frame AND the scrub bar has a range to travel, which is what
// tells a reader this is a clip and not a still.
const previewIsReady = () => {
  const bar = document.getElementById('open-image-frame-scrub');
  if (!bar || bar.style.display === 'none' || !(Number(bar.max) > 0)) return false;
  const video = [...document.querySelectorAll('#open-image-crop-stage video')]
    .find((el) => el.style.display !== 'none');
  return !!video && video.readyState >= 2;
};

const seekSettled = () => {
  const video = [...document.querySelectorAll('#open-image-crop-stage video')]
    .find((el) => el.style.display !== 'none');
  return !!video && video.currentTime > 0 && !video.seeking;
};

export function makeVideoSteps({ config, runner, pages, clipPath, clipUrl }) {
  const { shared, blank, openModal, closeModal } = pages;
  const loadMs = config.get('timeouts.loadMs');

  const previewReady = (page) => page.waitForFunction(previewIsReady, null, { timeout: loadMs });

  // Park the playhead mid-clip: at frame 0 the bar reads as empty, and the shot would not
  // show that the accent fill tracks the position.
  const scrubToMiddle = async (page) => {
    const scrub = page.locator('#open-image-frame-scrub');
    await scrub.fill(String(Math.floor(Number(await scrub.getAttribute('max')) / 2)));
    await page.waitForFunction(seekSettled, null, { timeout: loadMs });
    await waitForAnimations(page);
  };

  const openWithClip = async (page) => {
    await blank(page);
    await openModal(page, 'open-image', OVERLAY);
    await page.locator('#open-image-file').setInputFiles(clipPath);
    await previewReady(page);
    await scrubToMiddle(page);
  };

  return Object.freeze([
    // Local file: the picked clip, its frame picker and the scrub bar under the player.
    { name: 'open-video-local', run: async (ctx, theme) => {
      const page = await shared(ctx, theme);
      await openWithClip(page);
      await runner.shot(page, 'open-video-local');
      await closeModal(page, OVERLAY);
    } },
    // The same clip over http, through the URL tab's explicit Preview.
    { name: 'open-video-url', run: async (ctx, theme) => {
      const page = await shared(ctx, theme);
      await blank(page);
      await openModal(page, 'open-image', OVERLAY);
      await page.locator('#oi-tab-url').click();
      await page.locator('#open-image-url').fill(clipUrl);
      await page.locator('#open-image-url-preview').click();
      await previewReady(page);
      await scrubToMiddle(page);
      await runner.shot(page, 'open-video-url');
      await closeModal(page, OVERLAY);
    } },
    // Crop before opening: the rect is drawn on the player itself, so the frame that lands
    // on the canvas is the one framed here.
    { name: 'crop-video', run: async (ctx, theme) => {
      const page = await shared(ctx, theme);
      await openWithClip(page);
      await page.locator('#open-image-crop-toggle').check();
      await page.locator('#open-image-crop-box').waitFor({ state: 'visible', timeout: loadMs });
      await waitForAnimations(page);
      await runner.shot(page, 'crop-video');
      await closeModal(page, OVERLAY);
    } },
  ]);
}
