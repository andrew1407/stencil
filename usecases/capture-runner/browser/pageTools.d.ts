import type { Browser, Page } from 'playwright';
import type { CaptureConfig, ThemeName } from '../lib/captureConfig.js';

export interface BrowserPages {
  fresh(theme: ThemeName): Promise<Page>;
  shared(ctx: { page?: Page }, theme: ThemeName): Promise<Page>;
  blank(page: Page, color?: string): Promise<unknown>;
  drawLines(page: Page): Promise<void>;
  openModal(page: Page, key: string, overlay: string): Promise<void>;
  closeModal(page: Page, overlay: string): Promise<void>;
  chatReplied(page: Page): Promise<void>;
  seedLlm(page: Page, baseUrl: string): Promise<unknown>;
  send(page: Page, text: string): Promise<void>;
  gotoApp(page: Page, opts?: object): Promise<Page>;
  settleModalAnimations(page: Page, overlayId: string): Promise<unknown>;
  expectModalOpen(page: Page, overlayId: string): Promise<void>;
}

export const gotoApp: (page: Page, opts?: object) => Promise<Page>;
export const settleModalAnimations: (page: Page, overlayId: string) => Promise<unknown>;
export const expectModalOpen: (page: Page, overlayId: string) => Promise<void>;
export const freezeMotion: (page: Page) => Promise<void>;
export const seedLlmSettings: (page: Page, baseUrl: string) => Promise<unknown>;
export const sendChat: (page: Page, text: string) => Promise<void>;

export function makeBrowserPages(opts: { config: CaptureConfig; browser: Browser }): BrowserPages;
