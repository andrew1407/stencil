export interface ShellPalette { bg: string; panel: string; panel2: string; line: string; text: string; muted: string; }
export interface ShellPalettes { dark: ShellPalette; light: ShellPalette; }
export interface AccentMap { [key: string]: string; }
/** The payload handed to mountStencilModal (JSON-serializable, structured-cloned across executeScript). */
export interface ShellTheme { mode: string; resolved: string; accent: string; palettes: ShellPalettes; accents: AccentMap; }

export declare const THEME_STORAGE_KEY: string;
export declare const RESOLVED_STORAGE_KEY: string;
export declare const THEME_MODES: string[];
export declare const SCHEMES: string[];
export declare const SHELL_PALETTES: ShellPalettes;

export declare function resolveShellMode(mode: string, prefersDark?: boolean): string;
export declare function shellPalette(mode: string, prefersDark?: boolean): ShellPalette;
export declare function shellAccent(accentKey: string): string;
export declare function loadShellTheme(): Promise<ShellTheme>;
export declare function injectedScheme(mode: string, resolved: string): 'system' | 'light' | 'dark';
