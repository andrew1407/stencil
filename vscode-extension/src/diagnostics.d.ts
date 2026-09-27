// Shape of diagnostics.js — the CLI's check lines and the parser's diagnostics, as squiggles.
import type { DiagnosticEntry } from './lib/scriptCheck.js';
export type { DiagnosticEntry };
export declare const CHECK_LINE: RegExp;
export declare const DEBOUNCE_MS: number;
export declare const parseCheckOutput: (text: string) => DiagnosticEntry[];
export declare const fromProgram: (program: unknown) => DiagnosticEntry[];
/** An entry, plus the calling `@use stencil` span of a template-body diagnostic. */
export type RelatedEntry = DiagnosticEntry & { related?: { line: number; col: number; len: number } };
export declare const toDiagnostic: (
  entry: Partial<RelatedEntry>, lines?: string[], uri?: unknown,
) => unknown;
export declare const collect: (
  document: unknown, options: { saved: boolean },
) => Promise<RelatedEntry[]>;
export declare const register: (context: unknown) => unknown;
