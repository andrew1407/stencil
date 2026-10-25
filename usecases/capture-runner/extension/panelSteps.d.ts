import type { Page } from 'playwright';
import type { ShotStep } from '../lib/shot/runner.js';
import type { PopupCtx } from './popup.js';

export function panelPage(ctx: PopupCtx & { panel?: Page }, theme: string): Promise<Page>;
export function rowEvent(panel: Page, name: string, type: 'dragstart' | 'dragend', at?: [number, number] | null): Promise<void>;
export function makePanelSteps(opts: { image: string }): readonly ShotStep[];
