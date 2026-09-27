// The line-list half of the wasm op map: the co-edit merge's keep mask, and the chain edits the
// parity suite drives against dragGestures.js.
import type { CoreMarshal } from './coreMarshal.js';

export interface PullTarget { kind: 'point' | 'segment'; ptIdx?: number; ptIdx2?: number; }
type ChainLine = { points: { x: number; y: number }[]; locked?: boolean; fillColor?: string };

export interface LineOps {
  /** keep[i]: local line i joins the merge; null past the codec's caps. */
  mergeLinesKeep(server: readonly unknown[], local: readonly unknown[]): boolean[] | null;
  unchainLine(line: ChainLine | null | undefined): boolean;
  pullOutPoint(line: ChainLine | null | undefined, target: PullTarget | null | undefined, x: number, y: number): number;
}

export declare const buildLineOps: (core: unknown, m: CoreMarshal) => LineOps;
