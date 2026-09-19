import type { CaptureConfig } from '../lib/captureConfig.js';
import type { ShotRunner, ShotStep } from '../lib/shotRunner.js';

export function makeActionSteps(opts: {
  config: CaptureConfig; runner: ShotRunner;
  still: (ctx: { page: unknown }, name: string) => Promise<unknown>;
}): readonly ShotStep[];
