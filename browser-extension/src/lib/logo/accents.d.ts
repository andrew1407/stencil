/** What the ported stage modules read off the page: the preset key and a custom hex, if any. */
export interface StageApp { readonly accent: string | null; readonly customAccent: string | null }
export declare const normalizeHex: (value: unknown) => string | null;
export declare const accentHex: (key: string | null | undefined) => string | null;
export declare const faviconSvg: (hex: string) => string;
export declare const pageApp: () => StageApp;
export declare const pageAccentHex: (app?: StageApp) => string | null;
