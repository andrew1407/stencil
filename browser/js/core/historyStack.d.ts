// Snapshot history: DrawingApp's `history` array + `historyStep` cursor semantics, deep-copied
// on push/undo/redo, with the "step 0 → the floor, step -1" undo stop. A snapshot is a Lines
// array or an editor memento; depth-capped at MAX_STEPS, shared with core/state/HistoryStack.hpp.
import type { CodecLine } from './line/linesCodec.js';
import type { CropRect } from './geometry.js';

export declare const MAX_STEPS: number;

/** One undo step of the editor: the lines, the crop and turn they sit on, and the filter over them. */
export interface EditorMemento {
  lines: Partial<CodecLine>[];
  cropRect: CropRect | null;
  rotationQuarters: number;
  /** Absent or '' on a step that leaves the filter as it is. */
  filter?: string;
  filterColor?: string;
}

export type Snapshot = Partial<CodecLine>[] | EditorMemento;

/** What a memento is read from: the app's own fields (its filter is `imageFilter`). */
export interface MementoSource {
  lines: Partial<CodecLine>[]; cropRect: CropRect | null; rotationQuarters: number;
  imageFilter?: string; filterColor?: string;
}

export declare const editorMemento: (app: MementoSource) => EditorMemento;
/** The step on screen: the one the cursor names, or the floor at step -1. */
export declare const cursorStep: (h: Pick<HistoryStack, 'history' | 'historyStep' | 'floor'>) => Snapshot | undefined;
/** True when `step` carries a filter and it is the one `app` shows. */
export declare const sameFilter: (step: Snapshot | null | undefined, app: Pick<MementoSource, 'imageFilter' | 'filterColor'>) => boolean;

export declare class HistoryStack {
  history: Snapshot[];
  historyStep: number;
  /** What undo at step 0 returns: `[]`, or a memento with no lines on the oldest view. */
  floor: Snapshot;
  constructor();
  /** baseStep omitted: 0 when the snapshot has lines, else -1 (no current snapshot). */
  reset(snapshot: Readonly<Snapshot>, baseStep?: number): void;
  /** Truncates any redo branch, then bounds the depth by evicting the oldest snapshots. */
  push(snapshot: Readonly<Snapshot>): void;
  canUndo(): boolean;
  canRedo(): boolean;
  /** The snapshot to apply, the floor at the empty stop, or null when nothing to undo. */
  undo(): Snapshot | null;
  redo(): Snapshot | null;
}
