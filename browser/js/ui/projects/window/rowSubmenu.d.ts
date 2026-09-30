// A row menu item's nested list: a second `.project-menu` flown out beside its item, right or
// left, on hover, click or ArrowRight, back on ArrowLeft / Escape.

/** One row menu entry; `items` nests a list under it instead of an action. */
export interface RowMenuItem {
  icon: string;
  label: string;
  danger?: boolean;
  onClick?: (at: DOMRect) => void;
  items?: RowMenuItem[];
}

export declare const menuItemButton: (it: RowMenuItem) => HTMLButtonElement;
export declare const createRowSubmenu: (trigger: HTMLElement, items: RowMenuItem[],
  pick: (child: RowMenuItem, at: DOMRect) => void) => { hide(): void; contains(target: EventTarget | null): boolean };
