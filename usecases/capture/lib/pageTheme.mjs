// Putting a resolved theme onto a page. The browser app answers to its window.stencil
// facade, every extension page to window.StencilTheme — both stamp <html data-theme>.
import { waitForAnimations } from './waits.mjs';

const emulate = (page, theme) => page.emulateMedia({ colorScheme: theme, reducedMotion: 'reduce' });

export async function applyAppTheme(page, theme) {
  await emulate(page, theme);
  await page.evaluate((mode) => { window.stencil.darkTheme = mode === 'dark'; }, theme);
  await waitForAnimations(page);
}

export async function applyShellTheme(page, theme) {
  await emulate(page, theme);
  await page.evaluate((mode) => window.StencilTheme?.set(mode), theme);
  await waitForAnimations(page);
}
