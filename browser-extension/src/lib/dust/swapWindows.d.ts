// Classic script (see accent.js): no ES exports — extends window.StencilKit with the palette swap's
// per-window reveals (browser motion/surface/themeSwapWindows.js).

export interface SwapWindowsKit {
  /** The selector of the open dialogs, menus and lists that each get their own swap reveal. */
  SWAP_WINDOWS: string;
  /** Names every open window for the view transition; `css` plays each one's reveal, `release` drops the names. */
  nameSwapWindows(page: { x: number; y: number } | null, mode: string, ms: number, ease: string,
                  selector?: string, doc?: Document, fromTop?: boolean): { css: string; release: () => void };
  /** Names and writes every open window's reveal for the swap about to start; returns the release. */
  swapWindows(page: { x: number; y: number } | null, fromTop?: boolean): () => void;
}
