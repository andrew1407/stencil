// The header mark under the aqua skin (config/svgArt.json aquaLogo).
export declare const AQUA_LOGO: { inner: string; svg: string };
/** `art` (AQUA_LOGO.inner or .svg) with its gel placeholders filled from `accent` on a face. */
export declare const aquaLogoArt: (art: string, accent: string | null | undefined, dark?: boolean) => string;
/** Swap the header mark under `root` to the gel art in `accent` (on), or back to the app's own (off). */
export declare const swapAquaLogo: (root: ParentNode | null | undefined, on: boolean,
  accent?: string | null, dark?: boolean) => void;
/** The favicon art while the skin is on, its gel in `hex`. */
export declare const aquaFavicon: (hex: string) => string;
