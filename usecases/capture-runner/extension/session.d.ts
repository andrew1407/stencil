import type { BrowserContext, Page } from 'playwright';
import type { CaptureConfig } from '../lib/captureConfig.js';
import type { ShotRunner } from '../lib/shot/runner.js';

export declare const config: CaptureConfig;
export declare const runner: ShotRunner;
export declare const VIEWS: Record<string, { width: number; height: number }>;
export declare const TIMEOUTS: Record<string, number>;
export declare const MIN_ROWS: number;
export declare const freezeMotion: (page: Page) => Promise<void>;
export declare const llmSettings: (baseUrl: string) => object;
export declare const app: { url: string; stop(): void };
export declare const site: { url: string; stop(): void };
export declare const stub: { url: string; queue(plan: unknown): void; close(): Promise<void> };
export declare const context: BrowserContext;
export declare const background: () => Promise<unknown>;
export declare const extId: string;
export declare const host: Page;
