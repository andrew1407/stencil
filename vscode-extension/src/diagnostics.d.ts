// Shape of diagnostics.js — the CLI's check lines and the parser's diagnostics, as squiggles.
import type { DiagnosticEntry } from './lib/scriptCheck.js';
export type { DiagnosticEntry };
export declare const CHECK_LINE: RegExp;
export declare const DEBOUNCE_MS: number;
export declare const parseCheckOutput: (text: string) => DiagnosticEntry[];
export declare const fromProgram: (program: unknown) => DiagnosticEntry[];
export declare const toDiagnostic: (entry: Partial<DiagnosticEntry>) => unknown;
export declare const collect: (
  document: unknown, options: { saved: boolean },
) => Promise<DiagnosticEntry[]>;
export declare const register: (context: unknown) => unknown;
