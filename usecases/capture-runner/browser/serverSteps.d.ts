import type { Browser } from 'playwright';
import type { CaptureConfig } from '../lib/captureConfig.js';
import type { Collab } from '../lib/collab.js';
import type { ShotRunner, ShotStep } from '../lib/shot/runner.js';
import type { BrowserPages } from './pageTools.js';

export function makeServerSteps(opts: {
  config: CaptureConfig;
  runner: ShotRunner;
  pages: BrowserPages;
  collab: Collab;
  browser: Browser;
}): readonly ShotStep[];
