import type { Locator, Page } from 'playwright';
import type { CaptureConfig, ThemeName } from './captureConfig.js';

export interface ShotStep<C = any> {
  name: string;
  run: (ctx: C, theme: ThemeName, name: string) => Promise<void>;
}

export interface ShotRunner<C = any> {
  out: string;
  wanted(name: string): boolean;
  themeOf(name: string): ThemeName;
  shot(target: Page | Locator, name: string, opts?: object): Promise<string>;
  play(steps: readonly ShotStep<C>[], ctx?: C): Promise<C>;
  finish(): void;
}

export function makeShotRunner<C = any>(opts: { config: CaptureConfig; out: string }): ShotRunner<C>;
