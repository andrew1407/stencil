// A row menu item's nested list: a second `.project-menu` flown out beside its item, right or
// left, on hover, click or ArrowRight, back on ArrowLeft / Escape; it may nest again.

/** One row menu entry; `items` nests a list under it instead of an action. */
export interface RowMenuItem {
  icon: string;
  label: string;
  danger?: boolean;
  onClick?: (at: DOMRect) => void;
  /** A second click inside the double-click window runs this instead of onClick. */
  onDoubleClick?: (at: DOMRect) => void;
  items?: RowMenuItem[];
}

export declare const menuItemButton: (it: RowMenuItem) => HTMLButtonElement;
export declare const wireActionItem: (b: HTMLElement, it: RowMenuItem,
  pick: (item: RowMenuItem, at: DOMRect) => void) => void;
export interface RowSubmenu { hide(): void; contains(target: EventTarget | null): boolean }
/** `siblings()` is its level's lists: showing this one folds the others (one flyout out per level). */
export declare const createRowSubmenu: (trigger: HTMLElement, items: RowMenuItem[],
  pick: (child: RowMenuItem, at: DOMRect) => void, siblings?: () => RowSubmenu[]) => RowSubmenu;
