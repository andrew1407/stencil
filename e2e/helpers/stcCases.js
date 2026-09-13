// Reads the shared .stc corpus (browser/js/config/script/fixtures/cases.txt) so the e2e
// specs drive the surfaces with the very scripts the core's own suites are proved on.
// A case is '=== <name>' followed by '--- script' and an optional '--- diagnostics'.
import { readFileSync, writeFileSync } from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const CASES = path.resolve(
  path.dirname(fileURLToPath(import.meta.url)),
  '../../browser/js/config/script/fixtures/cases.txt',
);

// One corpus diagnostic: 'line:col:len: severity: message [CODE]'.
const DIAG = /^(\d+):(\d+):(\d+): (error|warning): (.*) \[([A-Z_]+)\]$/;

const readCases = () => {
  const cases = new Map();
  let current = null;
  let section = '';
  for (const line of readFileSync(CASES, 'utf8').split('\n')) {
    if (line.startsWith('=== ')) {
      current = { script: [], diagnostics: [] };
      section = '';
      cases.set(line.slice(4).trim(), current);
    } else if (!current) continue;
    else if (line.startsWith('--- ')) section = line.slice(4).trim();
    else if (section === 'script') current.script.push(line);
    else if (section === 'diagnostics') current.diagnostics.push(line);
  }
  return cases;
};

/** A case as `{ script, diagnostics: [{ line, col, len, severity, message, code }] }`. */
export function stcCase(name) {
  const found = readCases().get(name);
  if (!found) throw new Error(`no .stc fixture case named '${name}'`);
  return {
    script: found.script.join('\n').replace(/\n+$/, ''),
    diagnostics: found.diagnostics.map((l) => DIAG.exec(l)).filter(Boolean).map((m) => ({
      line: Number(m[1]), col: Number(m[2]), len: Number(m[3]),
      severity: m[4], message: m[5], code: m[6],
    })),
  };
}

/** Write a case's script into `dir` as <name>.stc; returns the absolute path. */
export function writeStcCase(name, dir) {
  const file = path.join(dir, `${name}.stc`);
  writeFileSync(file, `${stcCase(name).script}\n`);
  return file;
}
