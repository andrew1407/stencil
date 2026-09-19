import type { CaptureConfig } from '../lib/captureConfig.js';
import type { ShotRunner, ShotStep } from '../lib/shotRunner.js';
import type { BrowserPages } from './pageTools.js';

export function makeVideoSteps(opts: {
  config: CaptureConfig; runner: ShotRunner; pages: BrowserPages;
  clipPath: string; clipUrl: string;
}): readonly ShotStep[];
