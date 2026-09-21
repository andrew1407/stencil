// Shape of crop.js — the two `@crop` forms, keys and positional insets.
import type { ScriptDiagnostic, ScriptOp } from './types.js';
import type { ScriptStmt } from './parser.js';
import type { EvalState } from './lineStyle.js';
export const argsCrop: (
  st: ScriptStmt, state: EvalState, op: ScriptOp, diags: ScriptDiagnostic[],
) => boolean;
