// Line-snapshot history: DrawingApp's `history` array + `historyStep` cursor semantics,
// deep-copied on push/undo/redo, with the "step 0 → empty lines, step -1" undo stop.
// Depth-capped at MAX_STEPS, shared with core/state/HistoryStack.hpp's MAX_STEPS.
import type { CodecLine } from './line/linesCodec.js';

export declare const MAX_STEPS: number;

export declare class HistoryStack {
  history: Partial<CodecLine>[][];
  historyStep: number;
  constructor();
  /** baseStep omitted: 0 when `lines` is non-empty, else -1 (no current snapshot). */
  reset(lines: readonly Partial<CodecLine>[], baseStep?: number): void;
  /** Truncates any redo branch, then bounds the depth by evicting the oldest snapshots. */
  push(lines: readonly Partial<CodecLine>[]): void;
  canUndo(): boolean;
  canRedo(): boolean;
  /** The lines to apply, `[]` at the empty stop, or null when nothing to undo. */
  undo(): Partial<CodecLine>[] | null;
  redo(): Partial<CodecLine>[] | null;
}
