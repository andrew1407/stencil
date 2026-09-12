export declare const INTERNAL_DRAG_TYPE: string;
export declare const dragPayloadKind: (types: readonly string[] | DOMStringList | null) => 'internal' | 'files' | 'url' | '';

export interface DragArmer { isArmed(): boolean; update(types: readonly string[] | DOMStringList | null): void; end(): void; }
export declare const createDragArmer: (opts: { setArmed: (on: boolean) => void }) => DragArmer;

export declare const sameSource: (a: string | null | undefined, b: string | null | undefined) => boolean;
export declare const isMediaFile: (file: File | null | undefined) => boolean;

export interface ScanRow {
  kind: 'img' | 'video'; src: string; videoUrl?: string; name: string; w: number; h: number;
  alt: string; opened: unknown[]; pinned: boolean; measured: boolean;
}
export declare const entryFromUrl: (src: string, opts?: { name?: string; kind?: string }) => ScanRow | null;
export declare const entryFromDrop: (payload: { kind: 'files'; files: File[] } | { kind: 'url'; url: string } | null,
  opts?: { items?: ScanRow[]; objectUrl?: ((file: File) => string) | null }) => ScanRow | null;

export interface DragMenuAction { id: string; label: string; icon: string; needsPixels: boolean; }
export declare const DRAG_MENU_ACTIONS: DragMenuAction[];
export declare const dragMenuActions: (entry: ScanRow | null | undefined) => DragMenuAction[];
export declare const dragActionAllowed: (entry: ScanRow | null | undefined, id: string) => boolean;
