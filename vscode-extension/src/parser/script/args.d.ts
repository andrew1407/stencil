// Shape of args.js — the per-directive argument grammars.
import type { ScriptDiagnostic, ScriptOp, ScriptToken } from './types.js';
import type { ScriptStmt } from './parser.js';
import type { EvalState } from './lineStyle.js';
export const whereOf: (st: ScriptStmt, fallbackText: string) => ScriptToken;
export const argsFilter: (st: ScriptStmt, op: ScriptOp, diags: ScriptDiagnostic[]) => boolean;
export const argsShape: (
  st: ScriptStmt, state: EvalState, locked: boolean, op: ScriptOp, diags: ScriptDiagnostic[],
) => boolean;
export const argsLayout: (st: ScriptStmt, op: ScriptOp, diags: ScriptDiagnostic[]) => boolean;
export const argsSave: (st: ScriptStmt, op: ScriptOp) => boolean;
export const argsFrame: (st: ScriptStmt, op: ScriptOp, diags: ScriptDiagnostic[]) => boolean;
