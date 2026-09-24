// The one place a command line is built. A terminal IS a shell, so every argument that came
// out of a document or a picker is quoted here, for the shell the user actually runs.
import { DEFAULT_KIND, shellFor, shellKind } from './shellQuote.js';

const TERMINAL_NAME = 'Stencil';

const quoteArg = (value, kind = DEFAULT_KIND) => {
  const { quote, safe } = shellFor(kind), text = String(value ?? '');
  return text !== '' && safe.test(text) ? text : quote(text);
};

const commandLine = (cli, args = [], kind = DEFAULT_KIND) => {
  const head = quoteArg(cli, kind);
  const lead = head === String(cli ?? '') ? '' : shellFor(kind).lead;
  return [`${lead}${head}`, ...args.map((arg) => quoteArg(arg, kind))].join(' ');
};

// Looked up by name, so a window reload finds the terminal the last session made.
const reuseTerminal = (vscode, cwd) => {
  const existing = vscode.window.terminals.find((t) => t.name === TERMINAL_NAME);
  if (existing) return existing;
  return vscode.window.createTerminal({ name: TERMINAL_NAME, cwd });
};

const runInTerminal = (vscode, { cli, args, cwd }) => {
  const kind = shellKind(vscode.env?.shell);
  const terminal = reuseTerminal(vscode, cwd);
  terminal.show(true);
  if (cwd) terminal.sendText(`${shellFor(kind).cd} ${quoteArg(cwd, kind)}`);
  terminal.sendText(commandLine(cli, args, kind));
  return terminal;
};

export { TERMINAL_NAME, commandLine, quoteArg, reuseTerminal, runInTerminal };
