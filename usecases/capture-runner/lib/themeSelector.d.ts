import type { ThemeBlock, ThemeMode, ThemeName } from './captureConfig.js';

export const THEME_RESOLVERS: Readonly<Record<ThemeMode, (step: string, theme: ThemeBlock) => ThemeName>>;
export function resolveThemeMode(mode: ThemeMode, step: string, theme: ThemeBlock): ThemeName;
export function makeThemePicker(theme: ThemeBlock): (step: string) => ThemeName;
export function pairNames(base: string): [string, string];
