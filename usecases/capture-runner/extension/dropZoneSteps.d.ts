import type { Page } from 'playwright';
import type { CaptureConfig } from '../lib/captureConfig.js';
import type { ShotRunner, ShotStep } from '../lib/shotRunner.js';

export function makeDropZoneSteps(opts: {
  config: CaptureConfig; runner: ShotRunner; host: Page;
  applyShellTheme: (page: Page, theme: string) => Promise<void>;
  timeouts: Record<string, number>;
}): readonly ShotStep[];
