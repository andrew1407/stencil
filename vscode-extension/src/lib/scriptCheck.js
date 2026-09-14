// The CLI's `--script-check` answer, and the grammar of the one line per diagnostic that
// cli/src/script/load.zig writes. execFile with an argv array and no shell, so the extension
// host is never blocked and no path is ever word-split.
'use strict';

const { execFile } = require('node:child_process');
const { dirname } = require('node:path');

// `{file}:{line}:{col}: {severity}: {message} [{CODE}]`. The file field is greedy, so a
// Windows drive letter stays in it.
const CHECK_LINE = /^(.*):(\d+):(\d+): (error|warning): (.*?)(?: \[([A-Z_]+)\])?$/;

// 0 is a clean script, 1 is one with errors; anything else is not an answer about the script.
const ANSWERED = new Set([0, 1]);

const parseCheckOutput = (text) => {
  const found = [];
  for (const raw of String(text ?? '').split('\n')) {
    const m = CHECK_LINE.exec(raw.trimEnd());
    if (!m) continue;
    found.push({
      line: Number(m[2]), col: Number(m[3]), len: 1,
      severity: m[4], message: m[5], code: m[6] ?? '',
    });
  }
  return found;
};

const fromProgram = (program) => program.diagnostics.map((d) => ({
  line: d.line, col: d.col, len: d.len || 1,
  severity: d.severity, message: d.message, code: d.code,
}));

/* Null whenever the CLI did not answer parsably — a missing or wrong binary, an unreadable
 * file, a version skew — so the caller falls back to the parser copies instead of clearing
 * every squiggle. An empty list is an answer only from a clean exit. */
const runCheck = (cli, path) => new Promise((settle) => {
  const options = { cwd: dirname(path), encoding: 'utf8', shell: false };
  execFile(cli, ['--script-check', path], options, (error, stdout, stderr) => {
    const status = error ? error.code : 0;
    const found = parseCheckOutput(`${stdout ?? ''}${stderr ?? ''}`);
    if (!ANSWERED.has(status)) settle(null);
    else settle(status === 0 || found.length ? found : null);
  });
});

module.exports = { ANSWERED, CHECK_LINE, fromProgram, parseCheckOutput, runCheck };
