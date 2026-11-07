/** 'screen' = the OS fullscreen plus the in-app layer; 'window' = the layer alone. */
export type FsKind = 'screen' | 'window';
export declare const FS_KINDS: readonly FsKind[];

export interface FsRowFace { label: string; icon: string; hk: string; }
/** One menu row for `kind`; the row of the live `mode` is its exit. */
export declare const fsRowFace: (kind: FsKind, mode: FsKind | null) => FsRowFace;

export interface FsButtonGesture {
  click(): void;
  contextmenu(): void;
  pressStart(p?: { x?: number; y?: number; button?: number }): void;
  pressMove(p?: { x?: number; y?: number }): void;
  pressEnd(): void;
  dispose(): void;
}
export declare const createFsButtonGesture: (opts?: {
  single: () => void;
  double: () => void;
  /** `held`: the hold opened it, so that press's release picks the row under it or closes. */
  openMenu: (held: boolean) => void;
  delay?: number;
  holdMs?: number;
  slop?: number;
  setTimer?: (fn: () => void, ms: number) => unknown;
  clearTimer?: (id: unknown) => void;
}) => FsButtonGesture;

/** The toggle's composed tooltip text before its own hotkey is appended. */
export declare const fsButtonTitle: (mode: FsKind | null, combo?: (id: string) => string) => string;
/** Glyph, tooltip and the lit state for `mode`. */
export declare const syncFsButton: (btn: HTMLElement | null, mode: FsKind | null) => void;

export interface FsButtonHandle {
  /** Wires the right-click / hold opener onto the button's copy under `root` (the fullscreen strip). */
  wireCopyIn(root: ParentNode | null): void;
  close(): void;
  isOpen(): boolean;
}
export declare const wireFullscreenButton: (
  btn: HTMLElement | null,
  opts: { getMode: () => FsKind | null; choose: (kind: FsKind) => void },
) => FsButtonHandle | null;
