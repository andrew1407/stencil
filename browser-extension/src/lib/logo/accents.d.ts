/** What the ported stage modules read off the page: the preset key and a custom hex, if any. */
export interface StageApp { readonly accent: string | null; readonly customAccent: string | null }
export declare const normalizeHex: (value: unknown) => string | null;
export declare const accentHex: (key: string | null | undefined) => string | null;
/** True on the dark face of `el` (the page's root by default). */
export declare const pageDark: (el?: Element | null) => boolean;
/** 'dark' | 'light', as logoStage.json names the face. */
export declare const pageFace: (el?: Element | null) => 'dark' | 'light';
/** The favicon in `hex`: the stamped skin's own mark, else the kit's. */
export declare const faviconSvg: (hex: string) => string;
export declare const pageApp: () => StageApp;
export declare const pageAccentHex: (app?: StageApp) => string | null;
