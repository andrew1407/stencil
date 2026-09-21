// The projects modal as ONE source string, for the suites that pin its text: the component
// plus the `js/ui/projects/` collaborators it reaches, transitively, in import order.
// Derived from the import lines, never a hardcoded list, so it can't drift.
import { readFileSync } from 'node:fs';

const PROJECTS = new URL('../../js/ui/projects/', import.meta.url);
const MODAL = new URL('window/projectsModal.js', PROJECTS);

// Every relative specifier that lands inside js/ui/projects/, as a path relative to it.
const partsOf = (src, from) => [...src.matchAll(/ from '(\.[^']+\.js)';$/gm)]
  .map((m) => new URL(m[1], from))
  .filter((u) => u.href.startsWith(PROJECTS.href))
  .map((u) => u.href.slice(PROJECTS.href.length));

export const projectsModalSource = () => {
  const out = [readFileSync(MODAL, 'utf8')];
  const seen = new Set();
  const walk = (names) => names.forEach((n) => {
    if (seen.has(n)) return;
    seen.add(n);
    const url = new URL(n, PROJECTS);
    const src = readFileSync(url, 'utf8');
    out.push(src);
    walk(partsOf(src, url));
  });
  walk(partsOf(out[0], MODAL));
  return out.join('\n');
};
