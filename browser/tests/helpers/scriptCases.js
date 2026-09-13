// Splits the shared corpus (js/config/script/fixtures/cases.txt) into its cases. The C++
// walker in core/tests/scriptFixtures.test.cpp reads the same file with the same rules —
// plain text, because core/ has no JSON parser.
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';

export const CASES_PATH = fileURLToPath(
  new URL('../../js/config/script/fixtures/cases.txt', import.meta.url),
);

/* → [{ name, script, dump, diagnostics }]. A case with no diagnostics section expects none;
 * every section's text keeps a trailing newline, the way the parsers emit it. */
export const readScriptCases = (path = CASES_PATH) => {
  const lines = readFileSync(path, 'utf8').split('\n');
  const cases = [];
  let current = null;
  let section = null;

  const flush = () => {
    if (!current) return;
    for (const key of ['script', 'dump', 'diagnostics']) {
      if (current[key] === null) continue;
      // A section ends at the next marker, so the blank line before it (and the file's own
      // final newline) belong to the file, not to the case.
      const body = current[key];
      while (body.length > 0 && body[body.length - 1] === '') body.pop();
      current[key] = body.length > 0 ? `${body.join('\n')}\n` : '';
    }
    if (current.diagnostics === null) current.diagnostics = '';
    cases.push(current);
  };

  for (const line of lines) {
    if (line.startsWith('=== ')) {
      flush();
      current = { name: line.slice(4).trim(), script: null, dump: null, diagnostics: null };
      section = null;
      continue;
    }
    if (!current) continue;   // the file's own header comment
    if (line === '--- script') { current.script = []; section = 'script'; continue; }
    if (line === '--- dump') { current.dump = []; section = 'dump'; continue; }
    if (line === '--- diagnostics') { current.diagnostics = []; section = 'diagnostics'; continue; }
    if (section) current[section].push(line);
  }
  flush();
  return cases;
};
