// Shape of diagnostics.js — the CLI's check lines and the parser's diagnostics, as squiggles.
import type { ScriptProgram } from './parser/index.js';
export interface DiagnosticEntry {
  line: number; col: number; len: number; severity: string; message: string; code: string;
}
export declare const CHECK_LINE: RegExp;
export declare const parseCheckOutput: (text: string) => DiagnosticEntry[];
export declare const fromProgram: (program: ScriptProgram) => DiagnosticEntry[];
export declare const toDiagnostic: (entry: Partial<DiagnosticEntry>) => unknown;
export declare const collect: (
  document: unknown, options: { saved: boolean },
) => Promise<DiagnosticEntry[]>;
export declare const register: (context: unknown) => unknown;
