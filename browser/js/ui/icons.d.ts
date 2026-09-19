/** name → inline-SVG inner markup (config/icons.json). */
export declare const ICONS: Record<string, string>;
/** The draw-mode toggle's two complete `<svg>` faces (line / rect). */
export declare const DRAW_MODE_ICON: Record<string, string>;

/** One glyph as a complete `<svg>` string; '' for an unknown name. */
export declare function icon(name: string, opts?: { size?: number; cls?: string; sw?: number }): string;

/** Flip a Select all/Deselect all button's label and glyph to match `all`. */
export declare const setSelectAllFace: (btn: Element | null, all: boolean) => void;

/** Turn a control's glyph one revolution clockwise, once, in answer to a click. */
export declare const spinIconOnce: (btn: Element | null) => void;
