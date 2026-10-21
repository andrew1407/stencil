import type { CaptureConfig } from './captureConfig.js';

export interface Collab {
  url: string;
  local: boolean;
  mint: (() => Promise<string>) | null;
  stop(): Promise<void>;
}

export function startCollab(config: CaptureConfig): Promise<Collab>;
