// Shape of scriptCheck.js — the CLI's --script-check answer and its line grammar.
import type { ScriptProgram } from '../parser/index.js';
export interface DiagnosticEntry {
  line: number; col: number; len: number; severity: string; message: string; code: string;
}
export declare const ANSWERED: ReadonlySet<number>;
export declare const CHECK_LINE: RegExp;
export declare const parseCheckOutput: (text: string) => DiagnosticEntry[];
export declare const fromProgram: (program: ScriptProgram) => DiagnosticEntry[];
export declare const runCheck: (cli: string, path: string) => Promise<DiagnosticEntry[] | null>;
