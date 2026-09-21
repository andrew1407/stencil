export interface DragEntryHint {
  kind: 'img' | 'video';
  src?: string;
  videoUrl?: string;
}

export declare const LOGO_DROP_HINT: string;
export declare function fileEntryHint(dt: DataTransfer | null | undefined): DragEntryHint | null;

export interface LogoDragMenuOptions {
  logoEl: HTMLElement;
  menuEl: HTMLElement;
  placeMenu: (x: number, y: number, origin: { x: number; y: number }) => void;
  closeSharedMenu: () => void;
  dragKind: (e: DragEvent) => string | null;
  getDraggingRow: () => unknown;
  onAction: (actionId: string, payload: unknown) => void;
  springMs: number;
  graceMs?: number;
  doc?: Document;
}

export interface LogoDragMenu {
  graceOnDragLeave(e: DragEvent): void;
  release(): void;
  dismiss(): void;
  armUpdate(types: readonly string[]): void;
  armEnd(): void;
  isOpen(): boolean;
}

export declare function createLogoDragMenu(opts: LogoDragMenuOptions): LogoDragMenu;
