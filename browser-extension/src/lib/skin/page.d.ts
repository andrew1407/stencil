// The page as a session skin touches it (browser twin: js/ui/skin/glyphs.js).
/** The selector of every glyph and mark a skin swaps. */
export declare const GLYPHS: string;
/** The selector of the header marks alone. */
export declare const MARKS: string;
/** The elements `selector` names at or under `root`, `root` included when it matches. */
export declare const glyphsAt: (root: ParentNode | null | undefined, selector?: string) => Element[];
/** 'logo' for a mark, else the first ic-* name `has` accepts; undefined for a glyph without art. */
export declare const glyphOf: (el: Element, has: (name: string) => boolean) => string | undefined;
/** Points the page's favicon link at `svg`; nothing without a link or an svg. */
export declare const setFavicon: (doc: Document, svg: string | null | undefined) => void;
