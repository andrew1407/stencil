import type { Browser } from 'playwright';
import type { CaptureConfig } from '../lib/captureConfig.js';
import type { ShotRunner, ShotStep } from '../lib/shotRunner.js';
import type { LlmStub } from '../lib/llmStub.js';
import type { BrowserPages } from './pageTools.js';

export function makeStillSteps(opts: {
  config: CaptureConfig; runner: ShotRunner; pages: BrowserPages;
  stub: LlmStub; appUrl: string; browser: Browser;
}): readonly ShotStep[];
