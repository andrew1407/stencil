// The aqua word over the real wire: typed into the bare page it stamps the gel skin and its glossy
// glyphs, keeps the theme and the motion and writes no store, gives an empty editor its wallpaper, its word and its
// name; the webcore word swaps it out and back; typed again it lifts.
import { test, expect } from '@playwright/test';
import { gotoApp } from '../../helpers/boot.js';

const root = (page) => page.locator('html');
const type = async (page, word) => {
  await page.locator('body').click({ position: { x: 4, y: 4 } });
  await page.keyboard.type(word);
};

test('the word dresses the page in aqua, fills the empty editor, swaps with webcore and lifts', async ({ page }) => {
  await gotoApp(page, { motion: 'slide' });
  const theme = await root(page).getAttribute('data-theme');

  await type(page, 'somefruitcompany');
  await expect(root(page)).toHaveAttribute('data-skin', 'aqua');
  await expect(root(page)).toHaveAttribute('data-theme', theme);
  await expect(root(page)).toHaveAttribute('data-motion', 'slide');
  await expect.poll(() => page.evaluate(() => window.stencil.lines.length)).toBe(7);
  const state = await page.evaluate(() => ({
    motion: JSON.parse(localStorage.getItem('drawingApp_motion')).mode,
    name: window.stencil.current?.name,
    size: window.stencil.imageSize,
    locked: window.stencil.layout.lines.every((l) => l.locked),
    font: getComputedStyle(document.body).fontFamily,
    glyph: document.querySelector('#theme-toggle svg.ic')?.getAttribute('viewBox'),
    gel: getComputedStyle(document.documentElement).getPropertyValue('--aq-gel-bottom').trim(),
  }));
  expect(state.motion).toBe('slide');
  expect(state.name).toBe('somefruitcompany');
  expect(state.size.width).toBe(1024);
  expect(state.locked).toBe(true);
  expect(state.font).toContain('Lucida Grande');
  expect(state.glyph).toBe('0 0 32 32');
  expect(state.gel).toMatch(/^#[0-9a-f]{6}$/);

  await type(page, 'webcore');
  await expect(root(page)).toHaveAttribute('data-skin', 'webcore');
  await expect(root(page)).toHaveAttribute('data-motion', 'none');
  await type(page, 'somefruitcompany');
  await expect(root(page)).toHaveAttribute('data-skin', 'aqua');
  await expect(root(page)).toHaveAttribute('data-motion', 'slide');

  await type(page, 'somefruitcompany');
  await expect(root(page)).not.toHaveAttribute('data-skin', /./);
  await expect(root(page)).toHaveAttribute('data-motion', 'slide');
  expect(await page.evaluate(() => window.stencil.lines.length)).toBe(7);
});
