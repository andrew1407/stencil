import type { Page } from 'playwright';
import type { ThemeName } from './captureConfig.js';

export function applyAppTheme(page: Page, theme: ThemeName): Promise<void>;
export function applyShellTheme(page: Page, theme: ThemeName): Promise<void>;
