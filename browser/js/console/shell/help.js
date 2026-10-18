// The /help text as the CLI lays it out (cli/src/console/uiText.txt @help): a section heading,
// then `/name arg` in a gutter and the help beside it. Pure; the view colours the kinds.
import { COMMANDS, SECTIONS, CLI_MODE } from './table.js';

export const HELP_GUTTER = 24;

export const helpLines = ({ cliMode = false } = {}) => {
  const out = [{ kind: 'plain', text: "Commands  (a leading '/' is optional)" }];
  for (const section of SECTIONS) {
    const rows = COMMANDS.filter((c) => c.section === section && (cliMode || !c.cliMode));
    if (!rows.length) continue;
    out.push({ kind: 'blank', text: '' }, { kind: 'section', text: section });
    for (const c of rows) {
      const head = `/${c.name}${c.arg ? ` ${c.arg}` : ''}`;
      // A head past the gutter still keeps one space before its help, as the Zig pins show.
      out.push({ kind: 'row', text: `${head.length >= HELP_GUTTER ? `${head} ` : head.padEnd(HELP_GUTTER)}${c.help}`, head, help: c.help });
    }
  }
  if (cliMode) out.push({ kind: 'blank', text: '' }, { kind: 'plain', text: `'/${CLI_MODE.offCommand}' or holding the logo leaves CLI mode` });
  return out;
};
