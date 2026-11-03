/** The selector of the open windows, menus and lists that each get their own swap reveal. */
export declare const SWAP_WINDOWS: string;
/** Names every open window for the view transition; `css` plays each one's reveal, `release` drops the names. */
export declare function nameSwapWindows(page: { x: number; y: number } | null, mode: string, ms: number,
                                        ease: string, selector?: string, doc?: Document,
                                        fromTop?: boolean): { css: string; release: () => void };
