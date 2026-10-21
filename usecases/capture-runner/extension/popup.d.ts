import type { Locator, Page } from 'playwright';

export interface PopupCtx { ui?: Page }
export function popupClip(ui: Page): Promise<{ clip: { x: number; y: number; width: number; height: number } }>;
export function regionClip(ui: Page, selectors: string[], pad?: number): Promise<{ clip: { x: number; y: number; width: number; height: number } }>;
export function foldFilters(ui: Page): Promise<void>;
export function atRealHeight(ui: Page, on: boolean): Promise<void>;
export function surface(rel: string, viewport: { width: number; height: number }, theme: string): Promise<Page>;
export function dockBesideSite(panel: Page): Promise<void>;
export function scanSite(ui: Page): Promise<void>;
export function popup(ctx: PopupCtx, theme: string): Promise<Page>;
export function reopenPopup(ctx: PopupCtx, theme: string): Promise<Page>;
export function rowNamed(ui: Page, name: string): Locator;
export const ROW: string;
export function markRow(ui: Page, name: string): Promise<void>;
export function openRowMenu(ui: Page, name: string): Promise<void>;
export function openFlyout(ui: Page, label: string): Promise<boolean>;
export function clickMenuItem(ui: Page, text: string, inFlyout?: boolean): Promise<void>;
export function openInTab(ctx: PopupCtx, theme: string, image: string, label: string, name: string): Promise<void>;
