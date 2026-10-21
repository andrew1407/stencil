import type { CaptureConfig } from '../lib/captureConfig.js';
import type { ShotRunner, ShotStep } from '../lib/shot/runner.js';
import type { BotChat } from './chat.js';

export interface BotServer { url: string; mint: (() => Promise<string>) | null; stop(): Promise<void> }

export function makeServerSteps(opts: {
  config: CaptureConfig; runner: ShotRunner; chat: BotChat; icon: string;
  server: BotServer | null; enabled: boolean;
}): readonly ShotStep<{ projectId?: string | null }>[];
