// The canvas viewport's own overlay scrollbars (js/ui/scrollbars.js): revealed by a
// scroll, thin grey pills at rest, and only the bar under the real pointer swells to the
// accent — the other stays grey — then both fade after the idle spell.
import { test, expect } from '@playwright/test';
import { gotoApp } from '../../helpers/boot.js';

test('only the hovered bar takes the accent, and the bars fade when idle', async ({ page }) => {
  await gotoApp(page);
  await page.evaluate(async () => {
    await window.stencil.blank('#ffffff', { size: { width: 1600, height: 1200 } });
    window.stencil.zoomLevel = 200;
  });
  await expect.poll(() => page.evaluate(() => window.stencil.zoomLevel)).toBe(200);
  await page.waitForTimeout(400);
  const viewport = page.locator('#canvas-viewport');
  const barY = page.locator('.canvas-sb-y');
  const barX = page.locator('.canvas-sb-x');
  const thumbColor = (bar) => bar.locator('.canvas-sb-thumb').evaluate((el) => getComputedStyle(el).backgroundColor);
  const thumbWidth = (bar) => bar.locator('.canvas-sb-thumb').evaluate((el) => getComputedStyle(el).width);
  const accent = await page.evaluate(() => {
    const probe = document.createElement('div');
    probe.style.color = getComputedStyle(document.documentElement).getPropertyValue('--accent').trim();
    document.body.appendChild(probe);
    const rgb = getComputedStyle(probe).color;
    probe.remove();
    return rgb;
  });

  // The native bars are gone from the viewport; a scroll reveals ours, both axes.
  expect(await viewport.evaluate((el) => getComputedStyle(el).scrollbarWidth)).toBe('none');
  await viewport.evaluate((el) => { el.scrollLeft = 200; el.scrollTop = 150; });
  await expect(barY).toHaveClass(/canvas-sb-on/);
  await expect(barX).toHaveClass(/canvas-sb-on/);
  const grey = await thumbColor(barY);
  expect(grey).not.toBe(accent);
  expect(await thumbWidth(barY)).toBe('6px');

  // Pointer on the vertical bar: ITS thumb goes accent and swells; the horizontal stays grey.
  const yBox = await barY.boundingBox();
  await page.mouse.move(yBox.x + yBox.width / 2, yBox.y + yBox.height / 2);
  await expect.poll(() => thumbColor(barY)).toBe(accent);
  await expect.poll(() => thumbWidth(barY)).toBe('9px');
  expect(await thumbColor(barX)).toBe(grey);
  // …and it never fades while the pointer rests on it.
  await page.waitForTimeout(1200);
  await expect(barY).toHaveClass(/canvas-sb-on/);

  // Off the bar: grey again, thin again, and both fade after the idle spell.
  const vpBox = await viewport.boundingBox();
  await page.mouse.move(vpBox.x + vpBox.width / 2, vpBox.y + vpBox.height / 2);
  await expect.poll(() => thumbColor(barY)).toBe(grey);
  await expect.poll(() => thumbWidth(barY)).toBe('6px');
  await expect(barY).not.toHaveClass(/canvas-sb-on/, { timeout: 3000 });
  await expect(barX).not.toHaveClass(/canvas-sb-on/);

  // The viewport keeps its pinned height across zooms — the bars never count as
  // content under it (pan.js sums the viewport's following siblings).
  const heightAt200 = await viewport.evaluate((el) => el.getBoundingClientRect().height);
  await page.evaluate(() => { window.stencil.zoomLevel = 50; });
  await expect.poll(() => page.evaluate(() => window.stencil.zoomLevel)).toBe(50);
  await page.waitForTimeout(300);
  expect(Math.abs((await viewport.evaluate((el) => el.getBoundingClientRect().height)) - heightAt200)).toBeLessThan(2);
  await page.evaluate(() => { window.stencil.zoomLevel = 200; });
  await expect.poll(() => page.evaluate(() => window.stencil.zoomLevel)).toBe(200);
  await page.waitForTimeout(300);

  // Dragging the vertical thumb scrolls the viewport.
  await viewport.evaluate((el) => { el.scrollTop = 0; });
  await expect(barY).toHaveClass(/canvas-sb-on/);
  const thumb = await barY.locator('.canvas-sb-thumb').boundingBox();
  await page.mouse.move(thumb.x + thumb.width / 2, thumb.y + thumb.height / 2);
  await page.mouse.down();
  await page.mouse.move(thumb.x + thumb.width / 2, thumb.y + thumb.height / 2 + 60, { steps: 6 });
  await page.mouse.up();
  await expect.poll(() => viewport.evaluate((el) => el.scrollTop)).toBeGreaterThan(0);
});
