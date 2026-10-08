import type { Browser } from 'playwright';
import type { CaptureConfig } from '../lib/captureConfig.js';
import type { ShotRunner, ShotStep } from '../lib/shot/runner.js';
import type { BrowserPages } from './pageTools.js';

export function makeGestureSteps(opts: {
  config: CaptureConfig; runner: ShotRunner; browser: Browser; pages: BrowserPages;
}): readonly ShotStep[];
