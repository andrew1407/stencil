// One console line against the facade: echo, parse, the secret words (CLI mode, slashed only),
// the table lookup, then the handler, whose lines and error print through the host. Shared by
// the console window and the CLI stage; the twin of cli/src/console/dispatch.zig.
import { parseCommandLine } from './grammar.js';
import { commandOf, completions } from './table.js';
import { createHandlers } from './handlers.js';

const MAX_HISTORY = 50;   // line_edit/history.zig: the last 50 lines, a repeat of the newest once
// A line that types a key (`/llm key <value>`) is shown masked and recalled bare (llm/key.zig).
const MASKED_ECHO = '/llm key ••••••••', MASKED_HISTORY = '/llm key';
const typesKey = (row, arg) => row?.name === 'llm' && /^key\s+\S/i.test(arg);

const linesOf = (out) => (out == null ? [] : Array.isArray(out) ? out : [out]);

export const createInterpreter = ({ app, host, extra = {}, stencil = null }) => {
  // The facade lands after the windows are wired, so the handlers bind on the first line typed.
  let handlers = null;
  const handlersNow = () => (handlers ??= createHandlers({ app, stencil: stencil ?? globalThis.window?.stencil, extra }));
  const past = [];
  let cursor = 0;

  const run = async (line) => {
    const text = String(line ?? '').trim();
    if (!text) return;
    const { word, arg, slashed } = parseCommandLine(text);
    // The echo as the Zig console prints it: the prompt and a known command's word in the accent,
    // the arguments plain; a secret word's line whole in gold.
    const secret = slashed && host.cliMode() && !!host.isSecret?.(word);
    const row = secret ? null : commandOf(word, { cliMode: host.cliMode() });
    const keyed = typesKey(row, arg);
    const shown = keyed ? MASKED_ECHO : text;
    const head = shown.slice(0, shown.search(/\s/) < 0 ? shown.length : shown.search(/\s/));
    if (secret) host.print([`> ${text}`], 'secret');
    else host.print([{ text: `> ${shown}`, head: row ? `> ${head}` : '> ' }], 'echo');
    const kept = keyed ? MASKED_HISTORY : text;
    if (past.at(-1) !== kept) past.push(kept);
    if (past.length > MAX_HISTORY) past.shift();
    cursor = past.length;
    try {
      if (slashed && host.cliMode() && host.secret(word)) return;
      if (!row) { host.print([`error: unknown command '${word}' — type 'help' for the command list`], 'error'); return; }
      const out = await handlersNow()[row.name](arg, host);
      const lines = linesOf(out);
      if (lines.length && typeof lines[0] === 'object') host.print(lines, 'help');
      else if (lines.length) host.print(lines, 'plain');
    } catch (err) {
      host.print([`error: ${err?.message ?? err}`], 'error');
    } finally {
      host.statusChanged?.();   // the header reads the picture again, as the Zig screen's does each frame
    }
  };

  // Tab, as line_edit/editing.zig completes: only the first word; one match fills it in with a
  // space after, several extend to their common prefix, an ambiguous one lists them. Null = as typed.
  const complete = (line) => {
    const typed = String(line ?? '');
    if (/\s/.test(typed)) return null;
    const slash = typed.startsWith('/') ? '/' : '';
    const base = typed.slice(slash.length).toLowerCase();
    const names = completions(base, { cliMode: host.cliMode() });
    if (!names.length) return null;
    if (names.length === 1) return `${slash}${names[0]} `;
    let lcp = names[0];
    for (const n of names) { let i = 0; while (i < lcp.length && i < n.length && lcp[i] === n[i]) i++; lcp = lcp.slice(0, i); }
    if (lcp.length > base.length) return `${slash}${lcp}`;
    host.print([names.map((n) => `/${n}`).join('  ')], 'plain');
    return null;
  };

  // What the line being typed is, for the prompt's colour (line_edit/render.zig): a known
  // command's word wears the accent, a secret word gold, anything else nothing.
  const classify = (line) => {
    const { word, slashed } = parseCommandLine(String(line ?? ''));
    if (!word) return '';
    if (slashed && host.cliMode() && host.isSecret?.(word)) return 'secret';
    return commandOf(word, { cliMode: host.cliMode() }) ? 'cmd' : '';
  };

  const history = {
    up: () => { if (cursor > 0) cursor--; return past[cursor] ?? ''; },
    down: () => { if (cursor < past.length) cursor++; return past[cursor] ?? ''; },
    atEnd: () => cursor >= past.length,
    get entries() { return [...past]; },
  };

  return { run, complete, classify, history, handlerNames: () => Object.keys(handlersNow()) };
};
