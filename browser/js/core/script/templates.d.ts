// Shape of templates.js — `@stencil` definitions and their expansion.
import type { ScriptDiagnostic } from './types.js';
import type { ScriptStmt, TemplateDef } from './parser.js';
export interface TemplateIndex {
  byName: Map<string, number>; nameLengths: Set<number>; longestWords: number;
}
export const templateIndex: (templates: TemplateDef[]) => TemplateIndex;
export const expandStencilUse: (
  use: ScriptStmt, templates: TemplateDef[], index: TemplateIndex, depth: number,
  budget: { used: number }, out: ScriptStmt[], diags: ScriptDiagnostic[],
) => boolean;
export const reportUnusedTemplates: (
  templates: TemplateDef[], diags: ScriptDiagnostic[],
) => void;
