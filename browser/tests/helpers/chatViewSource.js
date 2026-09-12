// The shared chat rendering as ONE source string, for the suites that pin its text:
// the barrel's modules, concatenated in ITS order — the order they had in the single
// file. Derived from the barrel's re-exports, never a hardcoded list, so it can't drift.
import { readFileSync } from 'node:fs';

const BARREL = new URL('../../js/ui/chatView.js', import.meta.url);

export const chatViewSource = () => {
  const barrel = readFileSync(BARREL, 'utf8');
  const parts = [...barrel.matchAll(/ from '(\.\/[A-Za-z]+\.js)';$/gm)]
    .map((m) => readFileSync(new URL(m[1], BARREL), 'utf8'));
  return [barrel, ...parts].join('\n');
};
