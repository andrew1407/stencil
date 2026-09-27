import type { Page } from 'playwright';

export interface PopupCtx { ui?: Page }
export function popupClip(ui: Page): Promise<{ clip: { x: number; y: number; width: number; height: number } }>;
export function surface(rel: string, viewport: { width: number; height: number }, theme: string): Promise<Page>;
export function popup(ctx: PopupCtx, theme: string): Promise<Page>;
export function reopenPopup(ctx: PopupCtx, theme: string): Promise<Page>;
export function openRowMenu(ui: Page, nth?: number): Promise<void>;
export function openFlyout(ui: Page, nth?: number): Promise<void>;
export function clickMenuItem(ui: Page, text: string, inFlyout?: boolean): Promise<void>;
export function openInTab(ctx: PopupCtx, theme: string, label: string, name: string): Promise<void>;
