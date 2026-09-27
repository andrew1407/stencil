// A saved project's image in the REAL browser's IndexedDB (browser/js/core/project/store/): stored
// as a Blob, read back after a reload as an object URL the canvas decodes, and turned back into a
// data URL before anything leaves the page — here, the .stencil export.
import { test, expect } from '@playwright/test';
import { APP_URL } from '../../../helpers/config.js';

// gotoApp clears localStorage on every navigation; a reload here must keep the registry.
const bootOnce = async (page) => {
  await page.addInitScript(() => {
    if (sessionStorage.getItem('e2e_booted')) return;
    localStorage.clear();
    sessionStorage.setItem('e2e_booted', '1');
  });
  await page.goto(APP_URL);
  await page.waitForFunction(() => !!window.stencil, null, { timeout: 15_000 });
};

const storedImage = (page) => page.evaluate(() => new Promise((resolve) => {
  const req = indexedDB.open('stencil_projects');
  req.onsuccess = () => {
    const tx = req.result.transaction('payloads', 'readonly').objectStore('payloads');
    const all = tx.getAllKeys();
    all.onsuccess = () => {
      const key = all.result.find((k) => String(k).startsWith('stencil_image_'));
      if (!key) return resolve(null);
      const get = tx.get(key);
      get.onsuccess = () => resolve({ blob: get.result?.blob instanceof Blob, size: get.result?.blob?.size ?? 0 });
    };
  };
  req.onerror = () => resolve(null);
}));

test('a project image is a Blob in IndexedDB, reloads, and exports as a data URL', async ({ page }) => {
  await bootOnce(page);
  const size = await page.evaluate(async () => {
    await window.stencil.blank('#3060c0', { size: { width: 40, height: 24 } });
    return window.stencil.imageSize;
  });
  await expect.poll(() => storedImage(page), { timeout: 10_000 }).toMatchObject({ blob: true });
  expect((await storedImage(page)).size).toBeGreaterThan(0);

  await page.reload();
  await page.waitForFunction(() => !!window.stencil, null, { timeout: 15_000 });
  await page.evaluate(() => window.stencil.getProjects({ archived: true })[0].open());
  await expect.poll(() => page.evaluate(() => window.stencil.imageSize), { timeout: 10_000 }).toEqual(size);

  const download = await Promise.all([
    page.waitForEvent('download'),
    page.evaluate(() => {
      try { delete window.showSaveFilePicker; } catch { window.showSaveFilePicker = undefined; }
      return window.stencil.saveProjectFile();
    }),
  ]).then(([d]) => d);
  let text = '';
  for await (const chunk of await download.createReadStream()) text += chunk;
  expect(JSON.parse(text).image.dataUrl).toMatch(/^data:image\/[a-z]+;base64,/);
});
