import type { Page } from 'playwright';

export function settle(ms: number): Promise<void>;
export function waitForAnimations(page: Page, selector?: string | null, timeout?: number): Promise<void>;
export function waitForStable<T>(page: Page, read: () => T,
  opts?: { idleMs?: number; timeoutMs?: number; gapMs?: number }): Promise<T | null>;
export function waitForCanvasChange(page: Page, before: string, timeout?: number): Promise<unknown>;
export function canvasSize(page: Page): Promise<string>;
