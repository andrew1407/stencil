// components.css and animations.css are split into css/components/ and css/animations/.
// Tests that assert on "the stylesheet" read the concatenation, in index.html's link
// order — derived from index.html, never a hardcoded list, so it cannot drift.
import { readFileSync } from 'node:fs';

const ROOT = new URL('../../', import.meta.url);
const html = readFileSync(new URL('index.html', ROOT), 'utf8');

const concat = (dir) => [...html.matchAll(/<link\s+rel="stylesheet"\s+href="([^"]+)"/g)]
  .map((m) => m[1])
  .filter((href) => href.startsWith(`css/${dir}/`))
  .map((href) => readFileSync(new URL(href, ROOT), 'utf8'))
  .join('\n');

export const COMPONENTS_CSS = concat('components');
export const ANIMATIONS_CSS = concat('animations');
