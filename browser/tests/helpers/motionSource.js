// The motion helpers as ONE source string, for the suites that pin their text: the
// barrel's sections, concatenated in ITS order — the order they had in the single file.
import { readFileSync } from 'node:fs';

const BARREL = new URL('../../js/ui/motion.js', import.meta.url);

export const motionSource = () => {
  const barrel = readFileSync(BARREL, 'utf8');
  const parts = [...barrel.matchAll(/^export \* from '(\.\/motion\/[^']+)';$/gm)]
    .map((m) => readFileSync(new URL(m[1], BARREL), 'utf8'));
  return [barrel, ...parts].join('\n');
};
