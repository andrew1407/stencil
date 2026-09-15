import type { ChildProcess } from 'node:child_process';
import type { Page } from 'playwright';
import type { CaptureConfig, ThemeName } from './captureConfig.js';

export function VS_DIR(config: CaptureConfig): string;
export function WORKSPACE(config: CaptureConfig): string;
export function CLI_BIN(config: CaptureConfig): string;

export declare class VsCodeHost {
  constructor(proc: ChildProcess, page: Page, config: CaptureConfig);
  readonly page: Page;
  static prepare(config: CaptureConfig, theme: ThemeName): void;
  static freePort(port: number, dir: string): Promise<void>;
  static launch(config: CaptureConfig, theme: ThemeName, opts?: { open?: string | null }): Promise<VsCodeHost>;
  waitUntilReady(hasEditor: boolean): Promise<void>;
  runCommand(command: string): Promise<void>;
  stop(): Promise<void>;
}
