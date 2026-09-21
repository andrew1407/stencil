// Shape of scriptTemplates.js — `@stencil` definitions and their expansion.
import type { ScriptDiagnostic } from './scriptTypes.js';
import type { ScriptStmt, TemplateDef } from './scriptParser.js';
export const templateIndex: (templates: TemplateDef[]) => Map<string, number>;
export const expandStencilUse: (
  use: ScriptStmt, templates: TemplateDef[], byName: Map<string, number>, depth: number,
  budget: { used: number }, out: ScriptStmt[], diags: ScriptDiagnostic[],
) => boolean;
export const reportUnusedTemplates: (
  templates: TemplateDef[], diags: ScriptDiagnostic[],
) => void;
