import type { Page } from 'playwright';

export interface CastFrame { file: string; at: number }
export interface Cast { frames: CastFrame[]; end: number }

export function startCast(page: Page, dir: string): Promise<{ stop(): Promise<Cast> }>;
