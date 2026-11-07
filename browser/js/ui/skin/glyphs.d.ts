// The glyphs and the header mark as a session skin swaps them (extension twin: lib/skin/page.js).
/** Redraw every glyph under `root` in the art ui/icons.js serves now; `on` = a skin's art. */
export declare const swapGlyphs: (root: ParentNode, on: boolean) => void;
/** The header mark under `root` dressed by `dress` (on; again to repaint), or put back as it was (off). */
export declare const swapLogoArt: (root: ParentNode | null | undefined, on: boolean, dress?: ((logo: SVGElement) => void) | null) => void;
