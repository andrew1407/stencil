export interface AccentMap { [key: string]: string; }

export declare const ACCENT_HEX: AccentMap;
export declare const DEFAULT_HL: string;
export declare const ACCENT_STORAGE_KEY: string;

export declare function resolveHighlightColor(setting: string | null | undefined, accentKey: string): string;
export declare function highlightColorValue(settings?: { highlightColor?: string } | null): Promise<string>;
