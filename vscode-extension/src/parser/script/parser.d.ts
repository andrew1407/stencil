// Shape of parser.js — tokens to statements, grouped into raw blocks.
import type { ScriptDiagnostic, ScriptToken } from './types.js';
export interface ScriptStmt {
  directive: string; args: ScriptToken[]; opensBlock: boolean;
  line: number; col: number; len: number;
}
export interface TemplateDef {
  name: string; body: ScriptStmt[]; arity: number;
  line: number; col: number; len: number; used: boolean;
}
export interface RawBlock { header: ScriptStmt | null; body: ScriptStmt[]; implicit: boolean }
export const parseScript: (tokens: ScriptToken[]) => {
  blocks: RawBlock[]; templates: TemplateDef[]; diagnostics: ScriptDiagnostic[];
};
