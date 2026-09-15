// Deterministic waits: a capture waits for the state it is about to photograph, not for a
// guessed number of milliseconds. `settle` is the deliberate exception — a declared pause
// from a config file, for motion no signal reports.
import { setTimeout as delay } from 'node:timers/promises';

const INFINITE = (animation) => animation.effect?.getComputedTiming?.().iterations === Infinity;

export const settle = (ms) => delay(ms);

// Every finite animation in the page (or inside `selector`) has finished playing.
export const waitForAnimations = (page, selector = null, timeout = 5000) => page.waitForFunction(
  (sel) => {
    const root = sel ? document.querySelector(sel) : document;
    if (!root) return false;
    const live = root.getAnimations({ subtree: true })
      .filter((a) => a.effect?.getComputedTiming?.().iterations !== Infinity);
    return live.every((a) => a.playState === 'finished' || a.playState === 'idle');
  },
  selector, { timeout },
).catch(() => {});

// Poll a page-side reader until it returns the same value `idleMs` in a row: the answer for
// a stream nobody signals the end of (a terminal buffer, a transcript being typed out).
export async function waitForStable(page, read, { idleMs = 700, timeoutMs = 20_000, gapMs = 150 } = {}) {
  const deadline = Date.now() + timeoutMs;
  let last = null;
  let since = 0;
  while (Date.now() < deadline) {
    const now = await page.evaluate(read);
    if (now === last) {
      since += gapMs;
      if (since >= idleMs) return now;
    } else {
      last = now;
      since = 0;
    }
    await delay(gapMs);
  }
  return last;
}

// A canvas whose decoded size differs from `before`: the "did the new source land?" gate.
export const waitForCanvasChange = (page, before, timeout = 15_000) => page.waitForFunction(
  (was) => {
    const canvas = document.getElementById('canvas');
    return !!canvas && canvas.width > 0 && `${canvas.width}x${canvas.height}` !== was;
  },
  before, { timeout },
);

export const canvasSize = (page) => page.evaluate(() => {
  const canvas = document.getElementById('canvas');
  return canvas ? `${canvas.width}x${canvas.height}` : '';
});
