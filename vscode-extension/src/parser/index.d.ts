// Shape of index.js — the parser copies' entry point: parse, dump, format diagnostics.
import type { ScriptBlock, ScriptDiagnostic, ScriptOp, ScriptToken } from './script/scriptTypes.js';
export interface ScriptProgram {
  tokens: ScriptToken[];
  diagnostics: ScriptDiagnostic[];
  blocks: ScriptBlock[];
  ops: ScriptOp[];
  readonly errorCount: number;
  readonly hasErrors: boolean;
}
export const parseScript: (text: string) => ScriptProgram;
export const scriptDump: (program: ScriptProgram) => string;
export const scriptDiagnostics: (program: ScriptProgram) => string;
