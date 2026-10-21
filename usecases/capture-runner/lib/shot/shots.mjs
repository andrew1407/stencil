// Taking the picture: one still, a screencast clip with a drawn pointer, and the frame loop
// for pages that only take screenshots.
import fs from 'node:fs';
import path from 'node:path';
import { setTimeout as delay } from 'node:timers/promises';
import { scratchPath } from '../paths.mjs';
import { castToGif, pairToGif } from '../gifTools.mjs';
import { startCast } from './cast.mjs';
import { pointerOnLoad } from './pointer.mjs';

// A CSS-pixel rectangle in the frames' own pixels: a headless screencast may send frames at CSS
// size whatever the device pixel ratio, so the scale is read off the first frame's PNG header.
const framePx = (rect, { frames }, size) => {
  if (!rect || !frames.length) return null;
  const scale = fs.readFileSync(frames[0].file).readUInt32BE(16) / size.width;
  return Object.fromEntries(Object.entries(rect).map(([k, v]) => [k, Math.round(v * scale)]));
};

const filmedPage = async (browser, size, dpr) => {
  const context = await browser.newContext({ viewport: size, deviceScaleFactor: dpr });
  await pointerOnLoad(context);
  return { context, page: await context.newPage() };
};

// `target` is a page or a locator; stills disable CSS animations.
export async function shoot(target, dir, name, opts = {}) {
  const file = path.join(dir, `${name}.png`);
  await target.screenshot({ path: file, animations: 'disabled', ...opts });
  console.log(`  ${name}.png`);
  return file;
}

// `drive(page, mark)` sets the page up and acts; awaiting `mark()` starts the camera, so the
// loading and seeding before it never reach the GIF. `dpr` is the device pixel ratio filmed at;
// `crop` (CSS px) keeps only the region the clip is about.
export async function recordGif(browser, dir, name, drive,
  { size = { width: 1280, height: 800 }, dpr = 2, crop = null, ...look } = {}) {
  const { context, page } = await filmedPage(browser, size, dpr);
  let cast = null;
  const mark = async () => { cast ??= await startCast(page, scratchPath('clips', name)); };
  await drive(page, mark);
  const recorded = cast && await cast.stop();
  await context.close();
  if (!recorded) throw new Error(`${name}: the clip never called mark()`);
  castToGif(recorded, path.join(dir, `${name}.gif`), { ...look, cropPx: framePx(crop, recorded, size) });
  console.log(`  ${name}.gif`);
}

// Two pages in two contexts (two people), filmed at once and shown side by side.
export async function recordPairGif(browser, dir, name, drive,
  { size = { width: 800, height: 600 }, dpr = 2, crop = null, ...look } = {}) {
  const sides = [await filmedPage(browser, size, dpr), await filmedPage(browser, size, dpr)];
  let casts = null;
  const mark = async () => {
    casts ??= await Promise.all(sides.map(({ page }, i) => startCast(page, scratchPath('clips', `${name}-${i}`))));
  };
  await drive(sides[0].page, sides[1].page, mark);
  const recorded = casts && await Promise.all(casts.map((cast) => cast.stop()));
  for (const { context } of sides) await context.close();
  if (!recorded) throw new Error(`${name}: the clip never called mark()`);
  pairToGif(recorded[0], recorded[1], path.join(dir, `${name}.gif`), { ...look, gap: 8, cropPx: framePx(crop, recorded[0], size) });
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
