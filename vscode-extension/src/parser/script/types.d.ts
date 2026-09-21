// Shape of types.js — the .stc value types and the caps shared with core/script/.
export type TokenKind =
  | 'comment' | 'directive' | 'keyword' | 'number' | 'unit'
  | 'color' | 'string' | 'param' | 'punct' | 'ident' | 'error';
export type OpKind =
  | 'open' | 'frame' | 'crop' | 'filter' | 'line' | 'rect' | 'layout' | 'save' | 'undo' | 'redo';
export type SourceKind = 'project' | 'file' | 'url' | 'dir' | 'glob';
export type Severity = 'error' | 'warning';

export interface ScriptToken { line: number; col: number; len: number; kind: TokenKind; text: string }
export interface ScriptDiagnostic {
  severity: Severity; code: string; line: number; col: number; len: number; message: string;
}
export interface ScriptOp {
  kind: OpKind; block: number; line: number; col: number; len: number; editIndex: number;
  strs: string[]; toks: string[]; nums: number[];
}
export interface ScriptBlock {
  source: string; kind: SourceKind; frame: number; opStart: number; opCount: number; line: number;
}
export interface LineStyle {
  color: string; style: string; fillColor: string; pointColor: string;
  thickness: number; pointSize: number;
}

export const MAX_LINES: number;
export const MAX_TOKENS: number;
export const MAX_OPS: number;
export const MAX_BLOCKS: number;
export const MAX_TEMPLATES: number;
export const MAX_TEMPLATE_DEPTH: number;
export const MAX_TEMPLATE_EXPANSIONS: number;
export const MAX_POINTS_PER_LINE: number;
export const MAX_SOURCE_CHARS: number;
export const TOKEN_KINDS: readonly TokenKind[];
export const OP_KINDS: readonly OpKind[];
export const SOURCE_KINDS: readonly SourceKind[];
export const DIRECTIVES: readonly string[];
export const defaultLineStyle: () => LineStyle;
export const isEditDirective: (d: string) => boolean;
export const classifySource: (spec: string) => SourceKind;
export const unquoteWord: (s: string) => string;
export const isUnitWord: (w: string) => boolean;
export const isStencilUse: (st: { args: { text: string }[] }) => boolean;
