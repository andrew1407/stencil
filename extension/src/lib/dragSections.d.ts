export declare const ASSISTANT_SECTION: string;
export declare const SEARCH_SECTION: string;
export declare const DRAG_KINDS: string[];
export declare const SPRING_DWELL_MS: number;
export declare const RESTORE_DELAY_MS: number;

export declare const sectionForDragPoint: (kind: string, sectionId: string,
  state?: { collapsed?: Record<string, boolean>; sections?: string[] }) => string;

export interface DragSectionOpener {
  pendingRestore(): string[];
  armedSection(): string;
  pointerOver(kind: string, sectionId: string): void;
  manualToggle(id: string): void;
  dropIn(id: string): void;
  end(): void;
  scheduleEnd(delay?: number): void;
}
export declare const createDragSectionOpener: (opts?: {
  sections?: string[];
  isCollapsed: (id: string) => boolean;
  expand: (id: string) => void;
  collapse: (id: string) => void;
  onOpen?: (id: string) => void;
  dwellMs?: number;
  timer?: typeof setTimeout;
  clearTimer?: typeof clearTimeout;
}) => DragSectionOpener;
