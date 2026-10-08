// The page as a given theme paints it: a still copy of the body for a shadow tree, under the page's
// own sheets re-rooted onto the copy's root.

/** The class on the copy's root, which every `:root` rule of the copy answers to. */
export declare const COPY_CLASS: string;
/** The CSS filter on the picture's canvases in the copy. */
export declare const PICTURE_FILTER: string;
/** A canvas larger than this many pixels is copied down to it. */
export declare const MAX_COPY_PX: number;
/** Marks an element no copy takes (whatever the copy is shown through). */
export declare const NO_COPY_ATTR: string;
/** What no copy holds, at any depth: a drag ghost, tooltips and dust-cloud layers (and every
 *  canvas that is not the picture's). */
export declare const UNCOPIED: string;

/** Every `:root` selector re-pointed at the copy's root. */
export declare const rerootCss: (text: string) => string;
/** Reads each linked sheet's own text once; a copy built after it adopts the sheets as written. */
export declare const readPageCss: (doc?: Document) => Promise<void[]>;
/** The page's enabled sheets as adoptable copies — as written where known, else as the CSSOM
 *  serializes them; the same list until the page's sheets change. */
export declare const pageSheets: (doc?: Document) => CSSStyleSheet[];
/** Resets on `host` every custom property `from` inherits, so the copy's root starts from none. */
export declare const cutInheritance: (host: HTMLElement, from: Element) => void;
/** Per element, the moving properties (geometry and visibility) its animations hold right now. */
export declare const animatedProps: (doc: Document) => Map<Element, Set<string>>;
/** `src`'s pixels into `copy`, at most MAX_COPY_PX of them; `picture` inverts the copy. */
export declare const copyPixels: (src: HTMLCanvasElement, copy: HTMLCanvasElement, picture?: boolean) => void;

export interface PageCopy {
  /** The copy's root, detached, carrying `data-theme` and the copy marker. */
  html: HTMLElement;
  /** Restores every scroll offset; call once the copy is connected. */
  settle(): void;
}

export declare const buildPageCopy: (doc: Document, opts: {
  theme: 'dark' | 'light';
  isPicture?(canvas: HTMLCanvasElement): boolean;
}) => PageCopy;
