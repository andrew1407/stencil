// A module as ONE source string, for the suites that pin its text: the entry, then the relative
// modules it imports in order, each once; `deep` follows theirs too, `within` keeps the walk inside
// one folder. Derived from the import lines, never a hardcoded list, so it can't drift.
import { readFileSync, readdirSync } from 'node:fs';

const JS = new URL('../../js/', import.meta.url);

export const moduleSource = (entry, { within = null, deep = false } = {}) => {
  const out = [];
  const seen = new Set();
  const visit = (url, top) => {
    if (seen.has(url.href)) return;
    seen.add(url.href);
    const src = readFileSync(url, 'utf8');
    out.push(src);
    if (!top && !deep) return;
    for (const m of src.matchAll(/ from '(\.[^']+\.js)';$/gm)) {
      const next = new URL(m[1], url);
      if (!within || next.href.startsWith(within.href)) visit(next, false);
    }
  };
  visit(entry, true);
  return out.join('\n');
};

// The shared chat rendering: the barrel and its modules, in the order the single file had them.
export const chatViewSource = () => moduleSource(new URL('ui/chat/view.js', JS));

// The projects modal and the js/ui/projects/ collaborators it reaches, transitively.
export const projectsModalSource = () => moduleSource(new URL('ui/projects/window/projectsModal.js', JS),
  { within: new URL('ui/projects/', JS), deep: true });

// ui/ is split into feature folders, so a bare module name is looked up, not assumed flat.
const UI_DIR = new URL('ui/', JS);
export const uiPath = (n) => {
  const walk = (d) => readdirSync(d, { withFileTypes: true }).flatMap((e) =>
    e.isDirectory() ? walk(new URL(`${e.name}/`, d)) : (e.name === n ? [new URL(e.name, d)] : []));
  return n.includes('/') ? new URL(n, UI_DIR) : walk(UI_DIR)[0];
};
export const uiSource = (name) => readFileSync(uiPath(name), 'utf8');
