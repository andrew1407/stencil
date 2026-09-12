// The modal shell as ONE source string, for the suites that pin its text: ui/base.js plus
// the sibling modules it re-exports its window machinery from (registry, flight, shell).
// Derived from base.js's own `export … from './x.js'` lines, so it cannot drift.
import { readFileSync } from 'node:fs';

const BASE = new URL('../../js/ui/base.js', import.meta.url);

export const modalShellSource = () => {
  const base = readFileSync(BASE, 'utf8');
  const parts = [...base.matchAll(/^export \{[^}]*\} from '\.\/([A-Za-z]+\.js)';$/gm)].map((m) => m[1]);
  return [base, ...parts.map((n) => readFileSync(new URL(`../../js/ui/${n}`, import.meta.url), 'utf8'))]
    .join('\n');
};
