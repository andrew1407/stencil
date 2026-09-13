// Port of core/script/scriptUndo.cpp + scriptHistory.cpp — the per-block edit ledger and
// the `@undo` / `@redo` statements that drive it.
import { makeDiag, tokenOfStmt } from './scriptDiagnostics.js';

/* Edits are numbered 1..n as written and never renumbered. Undoing marks an edit dead; the
 * ledger then RE-DERIVES the image state at each @save by emitting one undo{steps} back to
 * the last agreeing edit and replaying the survivors. No adapter computes an undo count. */
export class EditLedger {
  #edits = [];

  #applied = [];

  #removed = []; // LIFO, what @redo brings back

  addEdit(op, text) {
    this.#edits.push({ op, text, live: true });
    this.#applied.push(this.#edits.length - 1);
    return this.#edits.length;
  }

  get editCount() {
    return this.#edits.length;
  }

  // Selector forms: 0 = the last live edit, N = that edit, -N = N-th from the end.
  undoIndex(selector) {
    const n = this.#edits.length;
    if (n === 0) return { ok: false, reason: 'there is no edit to undo in this block' };

    let idx;
    if (selector === 0) {
      idx = -1;
      for (let i = n - 1; i >= 0; i -= 1) if (this.#edits[i].live) { idx = i; break; }
      if (idx < 0) return { ok: false, reason: 'every edit in this block is already undone' };
    } else if (selector > 0) {
      idx = selector - 1;
    } else {
      idx = n + selector;
    }
    if (idx < 0 || idx >= n) {
      return { ok: false, reason: `edit ${selector} is outside this block's ${n} edit(s)` };
    }
    if (!this.#edits[idx].live) {
      return { ok: false, reason: `edit ${idx + 1} is already undone` };
    }
    this.#edits[idx].live = false;
    this.#removed.push(idx);
    return { ok: true };
  }

  undoByText(text) {
    let found = -1;
    let matches = 0;
    for (let i = this.#edits.length - 1; i >= 0; i -= 1) {
      if (this.#edits[i].text !== text) continue;
      matches += 1;
      if (found < 0 && this.#edits[i].live) found = i;
    }
    if (found < 0) {
      return {
        ok: false,
        ambiguous: false,
        reason: matches > 0 ? 'that edit is already undone' : `no edit in this block matches '${text}'`,
      };
    }
    this.#edits[found].live = false;
    this.#removed.push(found);
    return { ok: true, ambiguous: matches > 1 };
  }

  redo(times) {
    if (this.#removed.length === 0) return { ok: false, reason: 'nothing to redo in this block' };
    for (let k = 0; k < times && this.#removed.length > 0; k += 1) {
      this.#edits[this.#removed.pop()].live = true;
    }
    return { ok: true };
  }

  // Emits the undo{steps} + replay that makes the applied state equal the live set.
  reconcile(out, block, line, col) {
    const live = [];
    for (let i = 0; i < this.#edits.length; i += 1) if (this.#edits[i].live) live.push(i);

    let k = 0;
    while (k < this.#applied.length && k < live.length && this.#applied[k] === live[k]) k += 1;

    const steps = this.#applied.length - k;
    if (steps > 0) {
      out.push({ kind: 'undo', block, line, col, len: 0, editIndex: 0, strs: [], toks: [], nums: [steps] });
    }
    for (let i = k; i < live.length; i += 1) out.push(this.#edits[live[i]].op);
    this.#applied = live;
  }

  // A new @frame starts a fresh base: nothing from before is undoable.
  reset() {
    this.#edits = [];
    this.#applied = [];
    this.#removed = [];
  }
}

// Built exactly like scriptLower's normalizedText, so a selector and the edit it names
// produce the same string.
const selectorText = (st) => {
  let out = st.args[0].text.slice(1).toLowerCase();
  for (let i = 1; i < st.args.length; i += 1) {
    if (st.args[i].kind === 'comment') continue;
    out += ` ${st.args[i].text.toLowerCase()}`;
  }
  return out;
};

export const applyHistoryStmt = (st, isRedo, ledger, diags) => {
  if (isRedo) {
    let times = 1;
    if (st.args.length > 0 && st.args[0].kind === 'number') times = parseInt(st.args[0].text, 10);
    if (!Number.isFinite(times) || times < 1) times = 1;
    const r = ledger.redo(times);
    if (!r.ok) {
      diags.push(makeDiag('warning', 'W_NOTHING_TO_REDO', tokenOfStmt(st), r.reason));
      return false;
    }
    return true;
  }

  if (st.args.length === 0) {
    const r = ledger.undoIndex(0);
    if (!r.ok) {
      diags.push(makeDiag('error', 'E_UNDO_NO_EDITS', tokenOfStmt(st), r.reason));
      return false;
    }
    return true;
  }

  if (st.args[0].kind === 'directive') {
    const r = ledger.undoByText(selectorText(st));
    if (!r.ok) {
      diags.push(makeDiag('error', 'E_UNDO_OUT_OF_RANGE', tokenOfStmt(st), r.reason));
      return false;
    }
    if (r.ambiguous) {
      diags.push(makeDiag('warning', 'W_AMBIGUOUS_UNDO', tokenOfStmt(st),
        'several edits match — the last one was undone'));
    }
    return true;
  }

  let any = false;
  for (const t of st.args) {
    if (t.kind === 'punct') continue;
    if (t.kind !== 'number') {
      diags.push(makeDiag('error', 'E_BAD_TOKEN', t, `'${t.text}' is not an edit number`));
      return false;
    }
    const selector = parseInt(t.text, 10);
    if (selector === 0) {
      diags.push(makeDiag('error', 'E_UNDO_OUT_OF_RANGE', t,
        'edits are numbered from 1; use -1 for the last one'));
      return false;
    }
    const r = ledger.undoIndex(selector);
    if (!r.ok) {
      diags.push(makeDiag('error', ledger.editCount === 0 ? 'E_UNDO_NO_EDITS' : 'E_UNDO_OUT_OF_RANGE',
        t, r.reason));
      return false;
    }
    any = true;
  }
  if (!any) {
    const r = ledger.undoIndex(0);
    if (!r.ok) {
      diags.push(makeDiag('error', 'E_UNDO_NO_EDITS', tokenOfStmt(st), r.reason));
      return false;
    }
  }
  return true;
};
