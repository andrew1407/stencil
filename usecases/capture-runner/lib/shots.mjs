// Taking the picture: one still, a recorded clip, and the frame loop for pages whose
// context cannot record video (a CDP-attached app).
import fs from 'node:fs';
import path from 'node:path';
import { setTimeout as delay } from 'node:timers/promises';
import { SCRATCH } from './paths.mjs';
import { webmToGif } from './gifTools.mjs';

// `target` is a page or a locator; stills disable CSS animations.
export async function shoot(target, dir, name, opts = {}) {
  const file = path.join(dir, `${name}.png`);
  await target.screenshot({ path: file, animations: 'disabled', ...opts });
  console.log(`  ${name}.png`);
  return file;
}

// The recording starts just before the first `mark()` call, so the page-load lead-in never
// reaches the GIF.
export async function recordGif(browser, dir, name, drive, { size = { width: 1280, height: 800 }, ...look } = {}) {
  const videos = path.join(SCRATCH, 'video', name);
  fs.rmSync(videos, { recursive: true, force: true });
  const context = await browser.newContext({ viewport: size, recordVideo: { dir: videos, size } });
  const startedAt = Date.now();
  const page = await context.newPage();
  let start = null;
  const mark = () => { if (start == null) start = Math.max(0, (Date.now() - startedAt) / 1000 - 0.4); };
  await drive(page, mark);
  const video = page.video();
  await context.close();
  webmToGif(await video.path(), path.join(dir, `${name}.gif`), { ...look, start });
  console.log(`  ${name}.gif`);
}

// A screenshot costs real time, so the rate asked for is never the rate achieved: the measured
// one comes back, and a GIF assembled at it plays at life speed.
export async function film(page, framesDir, ms, everyMs = 100, shotOpts = {}) {
  fs.mkdirSync(framesDir, { recursive: true });
  const startedAt = Date.now();
  let frames = 0;
  while (Date.now() - startedAt < ms) {
    frames += 1;
    await page.screenshot({ path: path.join(framesDir, `frame-${String(frames).padStart(4, '0')}.png`), ...shotOpts });
    await delay(everyMs);
  }
  const elapsed = Date.now() - startedAt;
  return { frames, ms: elapsed, fps: Math.max(1, Math.round((frames * 1000) / elapsed)) };
}
