// Open Image dialog helpers shared by the tests/browser/openImage/ specs: the modal opened on a
// booted app, the crop rows' locators, and a ratio chosen through the rendered custom select.
import { expect } from '@playwright/test';
import { gotoApp } from './boot.js';

export const sizeRow = (page) => page.locator('#open-image-crop-size-row');
export const sizeSel = (page) => page.locator('#open-image-crop-size');
export const customGroup = (page) => page.locator('#open-image-crop-size-custom');
export const customW = (page) => page.locator('#open-image-crop-size-w');
export const customH = (page) => page.locator('#open-image-crop-size-h');
export const dims = (page) => page.locator('#open-image-crop-dims');
export const toggle = (page) => page.locator('#open-image-crop-toggle');
export const cropRow = (page) => page.locator('#open-image-crop-row');
export const cropBox = (page) => page.locator('#open-image-crop-box');

// Every <select> wears js/ui/control/customSelect.js — the native node is hidden but still the source of
// truth, so choosing its option through the rendered menu is what drives the change handler.
export async function chooseRatio(page, text) {
  await sizeSel(page).locator('xpath=..').locator('.accent-dd-trigger').click();
  const menu = page.locator('.accent-dd-menu:visible');
  await menu.locator('.accent-dd-opt', { hasText: text }).click();
}

export async function openModal(page) {
  await gotoApp(page);
  await page.locator('#load-image-btn').click();
  await expect(page.locator('#open-image-modal-overlay')).toHaveClass(/modal-open/);
}
