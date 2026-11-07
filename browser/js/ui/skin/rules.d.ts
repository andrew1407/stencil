// The rules both skins' tables share (desktop twin: support/skin/skinTable.hpp).
import type { CodecLine } from '../../core/line/linesCodec.js';

export interface CellRun { x: number; y: number; w: number; h: number; color: string }
export interface CellGridBase { cols: number; rows: number }
/** Same-colour runs along each row of a `grid`, `colorAt(cx, cy, grid)` naming each cell's colour. */
export declare const colorRuns: <G extends CellGridBase>(grid: G, colorAt: (cx: number, cy: number, grid: G) => string) => CellRun[];
/** The word's glyph table and its lettering style. */
export interface WordTable { text: string; glyphs: Record<string, [number, number][]>; grid: [number, number]; gapCells: number }
export interface WordStyle { widthShare: number; centerYShare: number; colors: string[]; stroke: string; thickness: number }
/** The word as closed, locked, filled lines across the top of a w×h image, one colour each. */
export declare const wordLinesOf: (w: number, h: number, word: WordTable, style: WordStyle) => CodecLine[];
