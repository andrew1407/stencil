/** An action, or with `items` a list nested beside it (rowSubmenu.js). */
export interface ProjectRowMenuItem {
  icon: string;
  label: string;
  danger?: boolean;
  onClick?: (anchorRect: DOMRect) => void;
  items?: ProjectRowMenuItem[];
}

export interface ProjectRowMenu {
  /** Opens under `anchor`, or at `point` for a right-click; `from` overrides where its dust flies. */
  showMenu(anchor: Element, items: (ProjectRowMenuItem | null | undefined)[], point?: { x: number; y: number } | null,
    opts?: { from?: { x: number; y: number } | null }): void;
  closeMenu(): void;
}

/** The per-row "⋯" menu, one floating node reused by every project row. */
export declare const createProjectRowMenu: () => ProjectRowMenu;
