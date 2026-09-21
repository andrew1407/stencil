/** Sets a checkbox/radio from code and animates the mark, like a real click would. */
export declare function setChecked(el: HTMLInputElement | null, value: boolean): void;

/** Writes a check glyph into `el`, dusting it in/out as it appears/disappears. */
export declare function swapCheckGlyph(el: HTMLElement | null, html: string): void;

/** One delegated 'change' listener over `root` that dusts every checkbox/radio it sees. */
export declare function installControlSwap(root?: Document | null): void;
