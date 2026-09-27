// Sandboxing variants / ask previews: both run their ops against the LIVE editor and then
// put it back — the history mark rewound, else crop/rotate undone on the model and the lines
// written back; only `blank`/`frame` replace the original and reload the pixel snapshot.
import type { Stencil } from '../../console/stencilApi.js';
import type { PlanAction } from './opPlan.js';
import type { CodecLine } from '../../core/line/linesCodec.js';
import type { EditorMemento } from '../../core/historyStack.js';

/** A mark carries the editor memento its rewind restores; the rest of it is the surface's own. */
export interface EditorMark { memento?: EditorMemento; }

/** The undo stack around a sandboxed run: a mark is its steps, cursor, floor and the view on screen. */
export interface EditorHistory<Mark extends EditorMark = EditorMark> {
  mark(): Mark;
  /** Puts the stack, its cursor and the marked memento back — the run leaves no undo step. */
  rewind(mark: Mark): void;
}

/**
 * What a sandboxed run may disturb besides the pixels, copied to plain data: an editor memento
 * read off the facade (its crop, rotated-original px, is what a crop variant is re-committed to;
 * its turn is the mark's, else 0), and the page and formula settings beside it.
 */
export interface EditorStateSnapshot extends EditorMemento {
  /** The history mark the restore rewinds to; null without the capability. */
  mark: EditorMark | null;
  filter: string;
  filterColor: string;
  lines: Array<Partial<CodecLine> & { points: Array<{ x: number; y: number }> }>;
  pageSize: string;
  allowFormulas: boolean;
  formulaX: string;
  formulaY: string;
  size: { width: number; height: number } | undefined;
}

export declare const captureEditorState: (stencil: Stencil, editorHistory?: EditorHistory) => EditorStateSnapshot;
/** The working image exported with the filter and the lines switched OFF — a restorable snapshot. */
export declare const capturePixels: (stencil: Stencil, exportImage: () => Promise<string>) => Promise<string>;
/** True when one of `actions` replaces the original pixels, so the restore must reload them. */
export declare const needsPixelSnapshot: (actions: ReadonlyArray<PlanAction> | null | undefined) => boolean;
/** Put the editor back. `actions` are the ops that actually RAN; `pixels` is only read when one of them replaced the original. */
export declare const restoreWorkingImage: (
  stencil: Stencil, pixels: string | null, state: EditorStateSnapshot,
  actions: ReadonlyArray<PlanAction> | null | undefined, editorHistory?: EditorHistory,
) => Promise<void>;
