// Sandboxing variants / ask previews: both run their ops against the LIVE editor and then
// put it back — crop/rotate undone on the model, settings and lines written back; only
// `blank`/`frame` replace the original and reload the pixel snapshot.
import type { Stencil } from '../console/stencilApi.js';
import type { PlanAction } from './opPlan.js';
import type { CodecLine } from '../core/line/linesCodec.js';

/** What a sandboxed run may disturb besides the pixels — copied to plain data. */
export interface EditorStateSnapshot {
  filter: string;
  filterColor: string;
  pageSize: string;
  allowFormulas: boolean;
  formulaX: string;
  formulaY: string;
  lines: Array<Partial<CodecLine> & { points: Array<{ x: number; y: number }> }>;
  size: { width: number; height: number } | undefined;
  /** The crop rect (rotated-original px) a crop variant is re-committed to. */
  cropRect: { x: number; y: number; w: number; h: number } | null;
}

export declare const captureEditorState: (stencil: Stencil) => EditorStateSnapshot;
/** The working image exported with the filter and the lines switched OFF — a restorable snapshot. */
export declare const capturePixels: (stencil: Stencil, exportImage: () => Promise<string>) => Promise<string>;
/** True when one of `actions` replaces the original pixels, so the restore must reload them. */
export declare const needsPixelSnapshot: (actions: ReadonlyArray<PlanAction> | null | undefined) => boolean;
/** Put the editor back. `actions` are the ops that actually RAN; `pixels` is only read when one of them replaced the original. */
export declare const restoreWorkingImage: (
  stencil: Stencil, pixels: string | null, state: EditorStateSnapshot,
  actions: ReadonlyArray<PlanAction> | null | undefined,
) => Promise<void>;
