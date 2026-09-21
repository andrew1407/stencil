// Shape of script.js — the .stc entry point: parse, dump, and resolve geometry.
import type { ScriptBlock, ScriptDiagnostic, ScriptOp, ScriptToken } from './script/types.js';
export interface ScriptProgram {
  tokens: ScriptToken[];
  diagnostics: ScriptDiagnostic[];
  blocks: ScriptBlock[];
  ops: ScriptOp[];
  readonly errorCount: number;
  readonly hasErrors: boolean;
}
export interface ResolvedShape {
  points: { x: number; y: number }[];
  thickness: number;
  pointSize: number;
}
export interface CropSpec {
  x1?: string; x2?: string; y1?: string; y2?: string; aspect?: string; album?: boolean;
}
export const parseScript: (text: string) => ScriptProgram;
export const scriptDump: (program: ScriptProgram) => string;
export const scriptDiagnostics: (program: ScriptProgram) => string;
export const resolveShape: (
  op: ScriptOp, size: { width: number; height: number; pxPerCm?: number },
) => ResolvedShape | null;
export const cropSpecOf: (op: ScriptOp) => CropSpec;
