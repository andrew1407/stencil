import type { Page } from 'playwright';
import type { CaptureConfig } from '../lib/captureConfig.js';
import type { ShotRunner } from '../lib/shot/runner.js';

export interface VsCtx { page: Page; host: { openFile(name: string): Promise<void>; runCommand(c: string): Promise<void> } }
export declare const config: CaptureConfig;
export declare const runner: ShotRunner<VsCtx>;
export declare const TIMEOUTS: Record<string, number>;
export declare const TYPE_DELAY: number;
export declare const EDITOR: readonly string[];
export declare const PANEL: string;
export declare const PALETTE: string;
export declare const FILE_ROW: string;
export declare const LABEL_GUTTER: number;
export declare const PARK: { x: number; y: number };
export function clipOf(page: Page, ...selectors: string[]): Promise<{ x: number; y: number; width: number; height: number }>;
export function still(ctx: VsCtx, name: string, ...selectors: string[]): Promise<unknown>;
export function stillTerminal(ctx: VsCtx, name: string, ...selectors: string[]): Promise<unknown>;
export function lineEnd(ctx: VsCtx, text: string): Promise<void>;
export function terminalText(): string;
