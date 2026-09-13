// Shape of scriptCrop.js — the two `@crop` forms, keys and positional insets.
import type { ScriptDiagnostic, ScriptOp } from './scriptTypes.js';
import type { ScriptStmt } from './scriptParser.js';
import type { EvalState } from './scriptLineStyle.js';
export const argsCrop: (
  st: ScriptStmt, state: EvalState, op: ScriptOp, diags: ScriptDiagnostic[],
) => boolean;
