// The one reader of config/console/commands.json: the browser's rows, their aliases and help,
// the CLI-mode words, and the lookup a typed word resolves through.
import TABLE from '../../../../common/config/console/commands.json' with { type: 'json' };

const SURFACE = 'browser';

export const COMMANDS = Object.freeze(TABLE.commands.filter((c) => c.surfaces.includes(SURFACE)));
export const SECTIONS = Object.freeze([...TABLE.sections]);
export const CLI_MODE = Object.freeze({ ...TABLE.cliMode });

const fold = (s) => String(s ?? '').trim().toLowerCase();
const inScope = (c, cliMode) => cliMode || !c.cliMode;

// The row a word names, by name or alias, in any casing; a CLI-mode row only while it is on.
export const commandOf = (word, { cliMode = false } = {}) => {
  const w = fold(word);
  if (!w) return null;
  return COMMANDS.find((c) => inScope(c, cliMode) && (c.name === w || c.aliases.includes(w))) ?? null;
};

// The command names starting with `prefix`, in table order, for Tab.
export const completions = (prefix, { cliMode = false } = {}) => {
  const p = fold(prefix);
  return COMMANDS.filter((c) => inScope(c, cliMode) && c.name.startsWith(p)).map((c) => c.name);
};
