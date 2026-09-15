import type { Browser } from 'playwright';
import type { CaptureConfig } from '../lib/captureConfig.js';
import type { ShotRunner, ShotStep } from '../lib/shotRunner.js';
import type { BrowserPages } from './pageTools.js';

export function makeClipSteps(opts: {
  config: CaptureConfig; runner: ShotRunner; browser: Browser;
  pages: BrowserPages & { stubUrl: string; queuePlan(plan: object): void };
}): readonly ShotStep[];
