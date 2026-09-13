// The one place a command line is built. A terminal IS a shell, so every argument — a file
// path, an image name, anything that came from a document or a picker — is quoted here
// before it is sent. Nothing else in this extension may compose shell text.
'use strict';

const TERMINAL_NAME = 'Stencil';

// Bytes a POSIX shell and cmd.exe both pass through untouched.
const SAFE = /^[A-Za-z0-9_@%+=:,./-]+$/;

/* Single-quote anything that is not plainly safe; a single quote inside is closed, escaped
 * and reopened. An empty argument becomes '' rather than vanishing. */
const quoteArg = (value) => {
  const text = String(value ?? '');
  if (text !== '' && SAFE.test(text)) return text;
  return `'${text.split("'").join("'\\''")}'`;
};

const commandLine = (cli, args = []) => [cli, ...args].map(quoteArg).join(' ');

// Looked up by name, so a window reload finds the terminal the last session made.
const reuseTerminal = (vscode, cwd) => {
  const existing = vscode.window.terminals.find((t) => t.name === TERMINAL_NAME);
  if (existing) return existing;
  return vscode.window.createTerminal({ name: TERMINAL_NAME, cwd });
};

const runInTerminal = (vscode, { cli, args, cwd }) => {
  const terminal = reuseTerminal(vscode, cwd);
  terminal.show(true);
  if (cwd) terminal.sendText(commandLine('cd', [cwd]));
  terminal.sendText(commandLine(cli, args));
  return terminal;
};

module.exports = { SAFE, TERMINAL_NAME, commandLine, quoteArg, reuseTerminal, runInTerminal };
