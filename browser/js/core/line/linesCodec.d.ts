/** A core Line as the codec produces it — every field explicit (see core/models.hpp). */
export interface CodecLine {
  points: { x: number; y: number }[];
  color: string; thickness: number; pointSize: number;
  style: string; locked: boolean; fillColor: string; pointColor: string;
  /** The user's label; '' = unnamed. */
  name: string;
  /** Kept, but neither drawn nor hit. */
  hidden: boolean;
}

/** What a missing field stands for: core/models.hpp's Line defaults. */
export declare const LINE_DEFAULTS: Readonly<Omit<CodecLine, 'points'>>;

/** The two buffers a snapshot crosses the core ABI in; see the layout in linesCodec.js. */
export interface EncodedLines { nums: Float64Array; text: Uint8Array; }

export function encodeLines(lines: readonly Partial<CodecLine>[]): EncodedLines;

/** Lengths are honoured, never trusted: a truncated buffer stops at the last whole line; NaN or
 *  huge counts never drive a loop, and lines/points are capped as sanitizeLines caps them. */
export function decodeLines(nums: Float64Array, text: Uint8Array): CodecLine[];
