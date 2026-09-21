// The context menu as ONE source string, for the suites that pin its text: the component plus
// the markup, keyboard and Assistant modules split out of it. Derived from its own imports,
// never a hardcoded list, so it cannot drift.
import { readFileSync } from 'node:fs';

const MENU = new URL('../../js/ui/contextMenu/contextMenu.js', import.meta.url);

export const contextMenuSource = () => {
  const menu = readFileSync(MENU, 'utf8');
  const seen = new Set();
  // Each specifier resolves against the file it was READ from, not the root: the ctx modules
  // sit in a folder of their own, so a nested import is relative to there.
  const pull = (src, base) => [...src.matchAll(/ from '((?:\.\.?\/)+(?:[A-Za-z]+\/)*(?:ctx|contextMenu)[A-Za-z]*\.js)';/g)]
    .map((m) => new URL(m[1], base))
    .filter((url) => !seen.has(url.href) && seen.add(url.href))
    .flatMap((url) => {
      const text = readFileSync(url, 'utf8');
      return [text, ...pull(text, url)];
    });
  return [menu, ...pull(menu, MENU)].join('\n');
};
