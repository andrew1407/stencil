export interface ProjectRowMenuItem {
  icon: string;
  label: string;
  danger?: boolean;
  onClick: (anchorRect: DOMRect) => void;
}

export interface ProjectRowMenu {
  /** Opens under `anchor`, or at `point` for a right-click. */
  showMenu(anchor: Element, items: (ProjectRowMenuItem | null | undefined)[], point?: { x: number; y: number } | null): void;
  closeMenu(): void;
}

/** The per-row "⋯" menu, one floating node reused by every project row. */
export declare const createProjectRowMenu: () => ProjectRowMenu;
