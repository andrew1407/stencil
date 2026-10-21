import type { CaptureConfig } from '../lib/captureConfig.js';
import type { ShotRunner, ShotStep } from '../lib/shot/runner.js';
import type { BotChat, ChatWaits } from './chat.js';
import type { BotSamples } from './samples.js';

export function makeBotSteps(opts: {
  config: CaptureConfig; runner: ShotRunner; chat: BotChat; icon: string;
  samples: BotSamples; withModel: boolean; waits: ChatWaits;
}): readonly ShotStep<{ started?: boolean; projectId?: string | null }>[];
