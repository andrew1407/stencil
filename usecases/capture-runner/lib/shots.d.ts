import type { Browser, Locator, Page } from 'playwright';
import type { GifLook } from './gifTools.js';

export function shoot(target: Page | Locator, dir: string, name: string, opts?: object): Promise<string>;
export function recordGif(browser: Browser, dir: string, name: string,
  drive: (page: Page, mark: () => void) => Promise<void>,
  opts?: GifLook & { size?: { width: number; height: number } }): Promise<void>;
export interface FilmResult { frames: number; ms: number; fps: number }
export function film(page: Page, framesDir: string, ms: number, everyMs?: number,
  shotOpts?: object): Promise<FilmResult>;
