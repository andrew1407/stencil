// The webcore word over the real wire: typed into the bare page it stamps the skin, keeps the
// theme, stills the motion for the session without touching the store, gives an empty editor its
// picture, its word and its name; typed again it lifts; a reload wears the user's own look.
import { test, expect } from '@playwright/test';
import { gotoApp } from '../../helpers/boot.js';

const root = (page) => page.locator('html');
const type = async (page) => {
  await page.locator('body').click({ position: { x: 4, y: 4 } });
  await page.keyboard.type('webcore');
};

test('the word dresses the page, fills the empty editor, and lifts again', async ({ page }) => {
  // The boot helper clears the store on every navigation, so the stored look is made the
  // way a user makes it: the motion seed it offers, and the toolbar's own theme toggle.
  await gotoApp(page, { motion: 'water' });
  await page.locator('#theme-toggle').click();
  await expect(root(page)).toHaveAttribute('data-theme', 'dark');

  await type(page);
  await expect(root(page)).toHaveAttribute('data-skin', 'webcore');
  await expect(root(page)).toHaveAttribute('data-theme', 'dark');
  await expect(root(page)).toHaveAttribute('data-motion', 'none');
  await expect.poll(() => page.evaluate(() => window.stencil.lines.length)).toBe(7);
  const state = await page.evaluate(() => ({
    theme: localStorage.getItem('drawingApp_theme'),
    motion: JSON.parse(localStorage.getItem('drawingApp_motion')).mode,
    name: window.stencil.current?.name,
    size: window.stencil.imageSize,
    locked: window.stencil.layout.lines.every((l) => l.locked),
    pixelIcons: document.querySelectorAll('svg.ic[viewBox="0 0 16 16"]').length,
    lineArt: document.querySelectorAll('svg.ic[viewBox="0 0 24 24"]').length,
  }));
  expect(state.theme).toBe('dark');
  expect(state.motion).toBe('water');
  expect(state.name).toBe('webcore');
  // Loaded like any picture, so the page aspect crops it: the width is the picture's own.
  expect(state.size.width).toBe(1024);
  expect(state.size.height).toBeGreaterThan(500);
  expect(state.locked).toBe(true);
  expect(state.pixelIcons).toBeGreaterThan(10);
  expect(state.lineArt).toBe(0);

  await type(page);
  await expect(root(page)).not.toHaveAttribute('data-skin', /./);
  await expect(root(page)).toHaveAttribute('data-theme', 'dark');
  await expect(root(page)).toHaveAttribute('data-motion', 'water');
  expect(await page.evaluate(() => document.querySelectorAll('svg.ic[viewBox="0 0 16 16"]').length)).toBe(0);
  expect(await page.evaluate(() => window.stencil.lines.length)).toBe(7);

  await type(page);
  await expect(root(page)).toHaveAttribute('data-skin', 'webcore');
  await page.reload();
  await gotoApp(page, { motion: 'water' });
  await expect(root(page)).not.toHaveAttribute('data-skin', /./);
  await expect(root(page)).toHaveAttribute('data-motion', 'water');
});
