import type { BrowserContext, Page } from 'playwright';

export function pointerOnLoad(target: BrowserContext | Page): Promise<void>;
export function pointerOnPage(page: Page): Promise<void>;
