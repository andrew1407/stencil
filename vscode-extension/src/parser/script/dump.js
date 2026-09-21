// Port of core/script/dump.cpp — the canonical text form the fixtures compare against.
// A change here re-records every `<name>.dump.txt`.

// Trailing zeros make a dump churn on a harmless refactor; print the shortest exact form.
const num = (v) => {
  if (Number.isFinite(v) && Number.isInteger(v)) return String(v);
  let s = String(v);
  while (s.length > 1 && s.endsWith('0')) s = s.slice(0, -1);
  if (s.endsWith('.')) s = s.slice(0, -1);
  return s;
};

const quoted = (s) => `"${s}"`;

export const dumpProgram = (program) => {
  let out = '';
  for (let bi = 0; bi < program.blocks.length; bi += 1) {
    const b = program.blocks[bi];
    out += `block ${num(bi)} ${b.kind} ${quoted(b.source)}\n`;

    for (let k = 0; k < b.opCount; k += 1) {
      const op = program.ops[b.opStart + k];
      out += `  op ${op.kind}`;
      if (op.editIndex > 0) out += ` edit=${num(op.editIndex)}`;
      for (const s of op.strs) out += ` ${quoted(s)}`;
      if (op.toks.length > 0) {
        out += ' [';
        out += op.toks.map((t) => (t === '' ? '_' : t)).join(' '); // '_' is an edge left unset
        out += ']';
      }
      for (const n of op.nums) out += ` ${num(n)}`;
      out += '\n';
    }
  }
  return out;
};

export const dumpDiagnostics = (program) =>
  program.diagnostics
    .map((d) => `${num(d.line)}:${num(d.col)}:${num(d.len)}: ${d.severity}: ${d.message} [${d.code}]\n`)
    .join('');
