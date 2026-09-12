import type { DrawingApp } from '../core/drawingApp.js';

export declare const THEME_STORAGE_KEY: string;
export declare const THEME_MODES: string[];

/** 'system' resolves against the OS now (or the injected `prefersDark`), never stored resolved. */
export declare const resolveThemeMode: (mode: string, prefersDark?: boolean) => 'dark' | 'light';

/** Writes theme/accent onto the document element and persists them; the app keeps the getters. */
export declare class AccentController {
  constructor(app: DrawingApp);
  setTheme(theme: string, originEl?: Element | null): void;
  setThemeMode(mode: string, originEl?: Element | null): void;
  readonly themeMode: string;
  updateThemeIcon(): void;
  applyGlyphContrast(hex: string): void;
  setAccent(key: string, originEl?: Element | null): void;
  announce(value: string): void;
  applyAccent(key: string, originEl?: Element | null): string;
  previewAccent(key: string, originEl?: Element | null): void;
  endAccentPreview(originEl?: Element | null): void;
  setCustomAccent(hex: string, originEl?: Element | null): string | null;
}
