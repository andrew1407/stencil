export declare const URL_DRAG_TYPES: string[];
export declare const DRAG_TYPES: string[];
export declare const isDropCandidate: (types: readonly string[] | DOMStringList | null) => boolean;
export declare const isVideoFile: (file: File | null | undefined) => boolean;
export interface FilesDrop { kind: 'files'; files: File[]; }
export interface UrlDrop { kind: 'url'; url: string; }
export declare const classifyDrop: (dt: DataTransfer | null) => FilesDrop | UrlDrop | null;
export declare const leftTarget: (root: { contains?: (n: unknown) => boolean }, relatedTarget: unknown) => boolean;
export interface DropTargetHandle { setOver(on: boolean): void; isOver(): boolean; }
export declare const wireDropTarget: (root: HTMLElement, opts: { highlight: HTMLElement;
  onDrop: (payload: FilesDrop | UrlDrop) => void }) => DropTargetHandle;
