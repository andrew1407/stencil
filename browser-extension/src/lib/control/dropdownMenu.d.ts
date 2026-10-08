// PORT of browser/js/ui/control/dropdownMenu.js (browser-extension/tests/portParity.test.js).
export interface Point { x: number; y: number; }
export declare const menuDustPoint: (trigger: HTMLElement | null | undefined) => Point | null;
/** Points the slide entrance's transform-origin at `point` (the caret the motes fly from). */
export declare const growFrom: (menu: HTMLElement | null, point: Point | null,
                                opts?: { left?: number | null; above?: boolean }) => void;
export declare const placeMenu: (menu: HTMLElement | null, trigger: HTMLElement | null) => void;
export declare const showMenu: (menu: HTMLElement | null, trigger: HTMLElement) => void;
export declare const hideMenu: (menu: HTMLElement | null) => void;

export interface DragPickMachine {
  /** Past the slop with the button held. */
  readonly active: boolean;
  press(x: number, y: number): void;
  /** The row under the pointer while dragging (the first move past the slop opens the list), else null. */
  move(x: number, y: number): Element | null;
  /** True when the release ended a drag: it picked the row under it, or closed the list off it. */
  release(x: number, y: number): boolean;
  abort(): void;
}

export declare const createDragPick: (deps: {
  open(): void;
  close(): void;
  isOpen(): boolean;
  rowAt(x: number, y: number): Element | null;
  inList(x: number, y: number): boolean;
  pick(row: Element): void;
  slop?: number;
}) => DragPickMachine;

/** The class the row under a press-drag wears, the latched hover the menus already style. */
export declare const DRAG_PICK_ROW_CLASS: string;

/** Press on `trigger`, drag and release on one of `menu`'s `rows` to pick it (its own click). */
export declare const wireDragPick: (trigger: HTMLElement, menu: HTMLElement, deps: {
  open(): void;
  close(): void;
  isOpen?(): boolean;
  enabled?(): boolean;
  rows?: string;
}) => DragPickMachine | null;
