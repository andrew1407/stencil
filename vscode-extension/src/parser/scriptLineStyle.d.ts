// Shape of scriptLineStyle.js — `@use`: a unit, a line style, or a template call.
import type { LineStyle, ScriptDiagnostic } from './scriptTypes.js';
import type { ScriptStmt } from './scriptParser.js';
export interface EvalState { unit: string; style: LineStyle }
export const argsUse: (
  st: ScriptStmt, state: EvalState, diags: ScriptDiagnostic[],
) => boolean;
