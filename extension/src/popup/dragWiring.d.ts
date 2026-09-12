// Shapes for popup/dragWiring.js — spring-loaded section drops, the logo's quick-action
// drag menu, and the page-level drag/drop listeners tying them together.
export interface DragSectionOpener {
  pendingRestore(): string[];
  armedSection(): string;
  pointerOver(kind: string, sectionId: string): void;
  manualToggle(id: string): void;
  dropIn(id: string): void;
  end(): void;
  scheduleEnd(delay?: number): void;
}

export interface LogoDragMenu {
  graceOnDragLeave(e: DragEvent): void;
  release(): void;
  dismiss(): void;
  armUpdate(types: readonly string[] | undefined | null): void;
  armEnd(): void;
  isOpen(): boolean;
}

export declare const dragSections: DragSectionOpener;
export declare const logoMenu: LogoDragMenu;
