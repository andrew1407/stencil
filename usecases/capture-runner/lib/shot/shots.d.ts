import type { Browser, Locator, Page } from 'playwright';
import type { GifLook } from '../gifTools.js';

export function shoot(target: Page | Locator, dir: string, name: string, opts?: object): Promise<string>;
export interface Rect { x: number; y: number; width: number; height: number }
export function recordGif(browser: Browser, dir: string, name: string,
  drive: (page: Page, mark: () => Promise<void>) => Promise<void>,
  opts?: GifLook & { size?: { width: number; height: number }; dpr?: number; crop?: Rect | null }): Promise<void>;
export function recordPairGif(browser: Browser, dir: string, name: string,
  drive: (left: Page, right: Page, mark: () => Promise<void>) => Promise<void>,
  opts?: GifLook & { size?: { width: number; height: number }; dpr?: number; crop?: Rect | null }): Promise<void>;
export interface FilmResult { frames: number; ms: number; fps: number }
export function film(page: Page, framesDir: string, ms: number, everyMs?: number,
  shotOpts?: object): Promise<FilmResult>;
