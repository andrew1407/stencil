// The context menu as ONE source string, for the suites that pin its text: the component plus
// the markup, keyboard and Assistant modules split out of it. Derived from its own imports,
// never a hardcoded list, so it cannot drift.
import { readFileSync } from 'node:fs';

const MENU = new URL('../../js/ui/contextMenu.js', import.meta.url);

export const contextMenuSource = () => {
  const menu = readFileSync(MENU, 'utf8');
  const seen = new Set();
  const pull = (src, base) => [...src.matchAll(/ from '(\.\/(?:ctx|contextMenu)[A-Za-z]*\.js)';/g)]
    .filter((m) => !seen.has(m[1]) && seen.add(m[1]))
    .flatMap((m) => {
      const text = readFileSync(new URL(m[1], base), 'utf8');
      return [text, ...pull(text, base)];
    });
  return [menu, ...pull(menu, MENU)].join('\n');
};
