// Shape of diagnostics.js — diagnostic construction and the "did you mean" suggester.
import type { ScriptDiagnostic, ScriptToken, Severity } from './types.js';
export interface StmtPosition { line: number; col: number; len: number; directive?: string }
export const editDistance: (a: string, b: string) => number;
export const didYouMean: (word: string, candidates: string[]) => string;
export const makeDiag: (
  severity: Severity, code: string, at: Partial<ScriptToken>, message: string,
) => ScriptDiagnostic;
export const formatDiagnostic: (file: string, d: ScriptDiagnostic) => string;
export const hasErrors: (diagnostics: ScriptDiagnostic[]) => boolean;
export const tokenOfStmt: (s: StmtPosition) => ScriptToken;
