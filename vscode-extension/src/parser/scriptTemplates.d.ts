// Shape of scriptTemplates.js — `@stencil` definitions and their expansion.
import type { ScriptDiagnostic } from './scriptTypes.js';
import type { ScriptStmt, TemplateDef } from './scriptParser.js';
export const expandStencilUse: (
  use: ScriptStmt, templates: TemplateDef[], depth: number,
  out: ScriptStmt[], diags: ScriptDiagnostic[],
) => boolean;
export const reportUnusedTemplates: (
  templates: TemplateDef[], diags: ScriptDiagnostic[],
) => void;
