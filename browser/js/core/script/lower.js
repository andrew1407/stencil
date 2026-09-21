// Port of core/script/lower.cpp — statements to the flat op stream.
import { argsFilter, argsFrame, argsLayout, argsSave, argsShape } from './args.js';
import { argsCrop } from './crop.js';
import { hasErrors, makeDiag, tokenOfStmt } from './diagnostics.js';
import { argsUse } from './lineStyle.js';
import { expandStencilUse, reportUnusedTemplates, templateIndex } from './templates.js';
import {
  MAX_OPS, MAX_SOURCE_CHARS, MAX_TEMPLATE_EXPANSIONS, SOURCE_KINDS, classifySource,
  defaultLineStyle, isEditDirective, isStencilUse,
} from './types.js';
import { EditLedger, applyHistoryStmt } from './undo.js';
import { joinWords } from './values.js';

// The edit directives: the op each lowers to, and the grammar that fills it.
const EDIT_OPS = {
  crop: { kind: 'crop', read: (st, state, op, diags) => argsCrop(st, state, op, diags) },
  filter: { kind: 'filter', read: (st, state, op, diags) => argsFilter(st, op, diags) },
  layout: { kind: 'layout', read: (st, state, op, diags) => argsLayout(st, op, diags) },
  line: { kind: 'line', read: (st, state, op, diags) => argsShape(st, state, false, op, diags) },
  rect: { kind: 'rect', read: (st, state, op, diags) => argsShape(st, state, true, op, diags) },
};

// What `@undo @line …` matches on: the directive plus its argument words.
const normalizedText = (s) => {
  let out = s.directive;
  for (const t of s.args) {
    if (t.kind === 'comment') continue;
    out += ` ${t.text.toLowerCase()}`;
  }
  return out;
};

const blankOp = (kind, st, block) => ({
  kind,
  block,
  line: st.line,
  col: st.col,
  len: st.len,
  editIndex: 0,
  strs: [],
  toks: [],
  nums: [],
});

export const lowerScript = (parsed) => {
  const diagnostics = parsed.diagnostics.slice();
  const blocks = [];
  const ops = [];
  const emptyBlocks = [];
  const byName = templateIndex(parsed.templates);
  let capped = false; // a replay that would pass MAX_OPS stops the whole script
  const budget = { used: 0 }; // the whole script fan-out, blocks included

  for (const raw of parsed.blocks) {
    const blockIndex = blocks.length;
    const block = {
      source: '',
      kind: 'project',
      frame: 0,
      opStart: ops.length,
      opCount: 0,
      line: raw.implicit ? 1 : raw.header.line,
    };

    if (!raw.implicit) {
      block.source = joinWords(raw.header.args);
      block.kind = classifySource(block.source);
      if (block.source.length > MAX_SOURCE_CHARS) {
        diagnostics.push(makeDiag('error', 'E_LIMIT_SOURCE', tokenOfStmt(raw.header),
          'the @source spec is too long'));
        block.source = block.source.slice(0, MAX_SOURCE_CHARS);
      }
      const open = blankOp('open', raw.header, blockIndex);
      open.strs = [block.source];
      open.nums = [SOURCE_KINDS.indexOf(block.kind)];
      ops.push(open);
    }

    // Expand templates first, so edit numbering counts what a template contributed.
    const body = [];
    for (const st of raw.body) {
      if (st.directive !== 'use' || !isStencilUse(st)) { body.push(st); continue; }
      expandStencilUse(st, parsed.templates, byName, 1, budget, body, diagnostics);
      if (budget.used > MAX_TEMPLATE_EXPANSIONS) { capped = true; break; }
    }
    if (capped) break; // the capped block is not recorded, so nothing of it dumps

    const state = { unit: 'px', style: defaultLineStyle() };
    const ledger = new EditLedger();
    let sawSave = false;
    let sawEdit = false;
    const frames = [];

    const rewindAndReplay = (line, col) => {
      if (ledger.reconcile(ops, blockIndex, line, col)) return true;
      diagnostics.push(makeDiag('error', 'E_LIMIT_OPS',
        raw.implicit ? null : tokenOfStmt(raw.header), 'the script has too many ops'));
      capped = true;
      return false;
    };

    for (const st of body) {
      const d = st.directive;

      if (d === 'use') {
        argsUse(st, state, diagnostics);
        continue;
      }

      if (d === 'frame') {
        if (raw.implicit) {
          diagnostics.push(makeDiag('error', 'E_FRAME_OUTSIDE_SOURCE', tokenOfStmt(st),
            '@frame needs a @source block naming a video'));
          continue;
        }
        const op = blankOp('frame', st, blockIndex);
        if (!argsFrame(st, op, diagnostics)) continue;
        const idx = op.nums[0];
        if (frames.includes(idx)) {
          diagnostics.push(makeDiag('error', 'E_DUPLICATE_FRAME', tokenOfStmt(st),
            `frame ${idx} is already used in this block`));
          continue;
        }
        frames.push(idx);
        if (!rewindAndReplay(st.line, st.col)) break;
        ledger.reset();
        ops.push(op);
        continue;
      }

      if (d === 'undo' || d === 'redo') {
        applyHistoryStmt(st, d === 'redo', ledger, diagnostics);
        continue;
      }

      if (d === 'save') {
        const op = blankOp('save', st, blockIndex);
        argsSave(st, op);
        if (!rewindAndReplay(st.line, st.col)) break;
        ops.push(op);
        sawSave = true;
        continue;
      }

      if (!isEditDirective(d)) {
        diagnostics.push(makeDiag('error', 'E_BAD_TOKEN', tokenOfStmt(st),
          `'@${d}' cannot be used here`));
        continue;
      }

      const { kind, read } = EDIT_OPS[d];
      const op = blankOp(kind, st, blockIndex);
      if (!read(st, state, op, diagnostics)) continue;
      if (ops.length >= MAX_OPS) {
        diagnostics.push(makeDiag('error', 'E_LIMIT_OPS', tokenOfStmt(st),
          'the script has too many ops'));
        break;
      }
      // Numbered before the ledger copies it, so a replayed edit dumps as the same edit.
      op.editIndex = ledger.editCount + 1;
      ledger.addEdit(op, normalizedText(st));
      ops.push(op);
      sawEdit = true;
    }

    if (!capped) rewindAndReplay(block.line, 1);
    if (capped) break; // the capped block is not recorded, so nothing of it dumps

    if (!raw.implicit && !sawEdit && !sawSave) emptyBlocks.push(raw.header);

    block.opCount = ops.length - block.opStart;
    blocks.push(block);
  }

  reportUnusedTemplates(parsed.templates, diagnostics);
  // A script with an error never runs, so "does nothing" would only add noise.
  if (!hasErrors(diagnostics)) {
    for (const header of emptyBlocks) {
      diagnostics.push(makeDiag('warning', 'W_EMPTY_BLOCK', tokenOfStmt(header),
        'this @source block does nothing'));
    }
  }
  return { blocks, ops, diagnostics };
};
