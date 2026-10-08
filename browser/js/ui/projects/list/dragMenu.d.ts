import type { ProjectRowMenuItem } from '../window/projectRowMenu.js';

/** px of slack around the ⋯ and each open list. */
export declare const DRAG_MENU_SLACK_PX: number;

export interface DragMenu {
  /** A row was picked up; `rowItems` builds its own menu, and none brings no ⋯. */
  begin(rowItems: (() => (ProjectRowMenuItem | null | undefined)[]) | null | undefined): void;
  /** The pointer moved: highlights the item under it; true while on the ⋯ or its menu. */
  track(x: number, y: number): boolean;
  /** Released: the item under the pointer runs; true when the ⋯ or its menu took the drop. */
  drop(x: number, y: number): boolean;
  /** The drag ended: the menu folds unless an item ran, and the ⋯ leaves. */
  end(): void;
  /** An item ran on the release: that drop reorders nothing and opens no zone. */
  readonly applied: boolean;
}

/** The drag-time "⋯" beside the projects window's title, its menu picked on release. */
export declare function createDragMenu(deps: {
  title: Element | null;
  showMenu(anchor: Element, items: (ProjectRowMenuItem | null | undefined)[], point?: { x: number; y: number } | null,
    opts?: { from?: { x: number; y: number } | null }): void;
  closeMenu(): void;
}): DragMenu;
