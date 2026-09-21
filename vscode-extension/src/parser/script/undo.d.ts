// Shape of undo.js — the per-block edit ledger and the @undo / @redo statements.
import type { ScriptDiagnostic, ScriptOp } from './types.js';
import type { ScriptStmt } from './parser.js';
export class EditLedger {
  addEdit(op: ScriptOp, text: string): number;
  get editCount(): number;
  undoIndex(selector: number): { ok: boolean; reason?: string };
  undoByText(text: string): { ok: boolean; ambiguous?: boolean; reason?: string };
  redo(times: number): { ok: boolean; reason?: string };
  reconcile(out: ScriptOp[], block: number, line: number, col: number): boolean;
  reset(): void;
}
export const applyHistoryStmt: (
  st: ScriptStmt, isRedo: boolean, ledger: EditLedger, diags: ScriptDiagnostic[],
) => boolean;
