// The projects modal as ONE source string, for the suites that pin its text: the component
// plus the `js/ui/projects/` collaborators it reaches, transitively, in import order.
// Derived from the import lines, never a hardcoded list, so it can't drift.
import { readFileSync } from 'node:fs';

const MODAL = new URL('../../js/ui/projectsModal.js', import.meta.url);
const partsOf = (src) => [...src.matchAll(/ from '\.\/([A-Za-z]+\.js)';$/gm)].map((m) => m[1]);

export const projectsModalSource = () => {
  const out = [readFileSync(MODAL, 'utf8')];
  const seen = new Set();
  const walk = (names) => names.forEach((n) => {
    if (seen.has(n)) return;
    seen.add(n);
    const src = readFileSync(new URL(`../../js/ui/projects/${n}`, import.meta.url), 'utf8');
    out.push(src);
    walk(partsOf(src));
  });
  walk([...out[0].matchAll(/ from '\.\/projects\/([A-Za-z]+\.js)';$/gm)].map((m) => m[1]));
  return out.join('\n');
};
