// The modal shell as ONE source string, for the suites that pin its text: ui/base.js plus
// the sibling modules it re-exports its window machinery from (registry, flight, shell).
// Derived from base.js's own `export … from './x.js'` lines, so it cannot drift.
import { readFileSync } from 'node:fs';

const BASE = new URL('../../js/ui/base.js', import.meta.url);

export const modalShellSource = () => {
  const base = readFileSync(BASE, 'utf8');
  // The specifier is resolved against base.js, so the modules may sit in a folder of their own.
  const parts = [...base.matchAll(/^export \{[^}]*\} from '((?:\.\.?\/)+(?:[A-Za-z]+\/)*[A-Za-z]+\.js)';$/gm)];
  return [base, ...parts.map((m) => readFileSync(new URL(m[1], BASE), 'utf8'))].join('\n');
};
