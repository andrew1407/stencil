// layout.css, components.css and animations.css are sheet SETS: css/layout/, css/components/,
// css/animations/. Tests that assert on "the stylesheet" read the concatenation, in
// index.html's link order — derived from index.html, never a hardcoded list, so it cannot drift.
import { readFileSync } from 'node:fs';

const ROOT = new URL('../../', import.meta.url);
const html = readFileSync(new URL('index.html', ROOT), 'utf8');

// `css/<name>.css` and every `css/<name>/…` both count, so a set reads the same before and
// after its sheet is split into a directory.
const concat = (name) => [...html.matchAll(/<link\s+rel="stylesheet"\s+href="([^"]+)"/g)]
  .map((m) => m[1])
  .filter((href) => href === `css/${name}.css` || href.startsWith(`css/${name}/`))
  .map((href) => readFileSync(new URL(href, ROOT), 'utf8'))
  .join('\n');

export const LAYOUT_CSS = concat('layout');
export const COMPONENTS_CSS = concat('components');
export const ANIMATIONS_CSS = concat('animations');

// The extension's twin sheets are sheet sets too; re-exported here so a parity assertion
// reaches both surfaces' stylesheets through one helper.
export { extensionAnimationsCss, extensionThemeCss } from './extensionCss.js';
