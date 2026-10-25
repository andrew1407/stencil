// A long table body as a window of the rows its scroller shows between two spacer rows.

/** Past this many rows a body is windowed; at most this many, every row is listed. */
export declare const VIRTUAL_ROWS: number;

export interface RowWindowOptions {
  columns: number;
  /** The attribute each built row carries its index in, e.g. 'data-pt-idx'. */
  indexAttr: string;
  build: (i: number) => HTMLTableRowElement;
  /** An unseen last row (visibility: collapse) holding the columns at the whole list's widths. */
  sizer?: () => HTMLTableRowElement;
  /** After each render, e.g. to re-apply row highlights. */
  onRender?: () => void;
}

export declare class RowWindow {
  constructor(body: HTMLTableSectionElement, opts: RowWindowOptions);
  readonly body: HTMLTableSectionElement;
  /** True while the body holds more than VIRTUAL_ROWS rows. */
  readonly active: boolean;
  readonly count: number;
  /** False for a short list (the caller lists every row); else renders the window and answers true. */
  show(count: number): boolean;
  /** Scrolls row `i` into view and renders it: the row, or null while a smooth scroll is under way. */
  reveal(i: number, behavior?: ScrollBehavior): HTMLTableRowElement | null;
  /** Row `i` when the window holds it, else null. */
  rendered(i: number): HTMLTableRowElement | null;
}
