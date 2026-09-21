// Shape of scriptLower.js — statements to the flat op stream.
import type { ScriptBlock, ScriptDiagnostic, ScriptOp } from './scriptTypes.js';
import type { RawBlock, TemplateDef } from './scriptParser.js';
export const lowerScript: (parsed: {
  blocks: RawBlock[]; templates: TemplateDef[]; diagnostics: ScriptDiagnostic[];
}) => { blocks: ScriptBlock[]; ops: ScriptOp[]; diagnostics: ScriptDiagnostic[] };
