import type { Frame } from 'playwright';
import type { ShotStep } from '../lib/shot/runner.js';
import type { PopupCtx } from './popup.js';

export const CROP_PAGE: string;
export function modalFrame(part: string, ready: () => boolean, timeout: number): Promise<Frame>;
export function reloadSite(ctx: PopupCtx, theme: string): Promise<void>;
export function makeSiteSteps(opts: { image: string }): readonly ShotStep[];
