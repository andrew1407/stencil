// The aqua skin's pure rules (browser twins: js/ui/aqua/rules.js, logo.js, icons.js).
/** The skin's table, a byte copy of common/config/aqua.json. */
export declare const AQUA: { tint: { base: string; light: Record<string, [string, number]>; dark: Record<string, [string, number]> }; tokens: Record<'light' | 'dark', Record<string, string>> } & Record<string, unknown>;
/** The gel mark: `inner` for a 64-grid header svg, `svg` whole; %1–%4 are the gel tokens. */
export declare const AQUA_LOGO: { inner: string; svg: string };
/** `a` moved toward `b` by `share` in sRGB, as CSS color-mix(in srgb) rounds it. */
export declare const mixHex: (a: string, b: string, share: number) => string;
/** The --aq-* gel tokens `accent` paints on the light or dark face (the table's base when null). */
export declare const gelTokens: (accent: string | null | undefined, dark?: boolean) => Record<string, string>;
/** Every token name gelTokens writes. */
export declare const GEL_NAMES: readonly string[];
/** `art` with its %1–%4 filled from the gel `accent` paints. */
export declare const aquaLogoArt: (art: string, accent: string | null | undefined, dark?: boolean) => string;
/** The favicon: the whole gel mark in `hex`. */
export declare const aquaFavicon: (hex: string | null | undefined) => string;
/** One icon's art with fresh gradient ids, or '' for a name the table lacks. */
export declare const aquaIconArt: (name: string) => string;
export declare const hasAquaIcon: (name: string) => boolean;
