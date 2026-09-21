// Shape of lower.js — statements to the flat op stream.
import type { ScriptBlock, ScriptDiagnostic, ScriptOp } from './types.js';
import type { RawBlock, TemplateDef } from './parser.js';
export const lowerScript: (parsed: {
  blocks: RawBlock[]; templates: TemplateDef[]; diagnostics: ScriptDiagnostic[];
}) => { blocks: ScriptBlock[]; ops: ScriptOp[]; diagnostics: ScriptDiagnostic[] };
