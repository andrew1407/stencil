// A long list body as a window of the rows its scroller shows between two spacers.

/** Past this many rows a body is windowed (the default threshold); at most this many, every row is listed. */
export declare const VIRTUAL_ROWS: number;

/** How one kind of list is laid out: its row tag, its spacer and the element that scrolls it. */
export interface RowWindowShape {
  /** The row element's tag, e.g. 'tr' or 'li'. */
  tag: string;
  /** True when rows are striped by nth-child, so a row after the top spacer keeps its parity. */
  stripes?: boolean;
  scroller(body: HTMLElement): HTMLElement | null | undefined;
  /** An empty node `px` tall, standing in for the rows it replaces. */
  spacer(px: number): HTMLElement;
  /** A sticky header's height a revealed row must clear; 0 when absent. */
  headPx?(body: HTMLElement): number;
}

export interface RowWindowOptions {
  shape: RowWindowShape;
  /** The attribute each built row carries its index in, e.g. 'data-pt-idx'. */
  indexAttr: string;
  build: (i: number) => HTMLElement;
  /** An unseen last row (visibility: collapse) holding the columns at the whole list's widths. */
  sizer?: () => HTMLElement;
  /** After each render, e.g. to re-apply row highlights. */
  onRender?: () => void;
  /** Past this many rows the body is windowed; VIRTUAL_ROWS when omitted. */
  threshold?: number;
  /** True while a row must stay put (e.g. mid-drag): scrolling renders nothing until it is false. */
  isHeld?: () => boolean;
}

export declare class RowWindow {
  constructor(body: HTMLElement, opts: RowWindowOptions);
  readonly body: HTMLElement;
  readonly threshold: number;
  readonly isHeld: () => boolean;
  /** True while the body holds more than `threshold` rows. */
  readonly active: boolean;
  readonly count: number;
  /** False for a short list (the caller lists every row); else renders the window and answers true. */
  show(count: number): boolean;
  /** Scrolls row `i` into view and renders it: the row, or null while a smooth scroll is under way. */
  reveal(i: number, behavior?: ScrollBehavior): HTMLElement | null;
  /** Row `i` when the window holds it, else null. */
  rendered(i: number): HTMLElement | null;
}
