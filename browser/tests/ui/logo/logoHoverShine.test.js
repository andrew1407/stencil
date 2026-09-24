// The header mark's hover under motion "none" and the webcore skin (which lays "none" over the
// session): no pulse and no rays, only a still grow and an edge glow (logoHover.css).
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';

const read = (f) => readFileSync(new URL(`../../../css/${f}`, import.meta.url), 'utf8');
const block = (css, selector) => {
  const at = css.indexOf(`${selector} {`);
  assert.ok(at >= 0, `rule not found: ${selector}`);
  return css.slice(at, css.indexOf('}', at));
};
const HOVER = read('animations/logoHover.css');

test('motion "none" stills the hovered mark: no loop, a small grow, an accent glow at its edges', () => {
  const rule = block(HOVER, ':root[data-motion="none"] .app-logo-wrap.logo-hover .app-logo');
  assert.match(rule, /animation: none !important/);
  assert.match(rule, /transform: scale\(1\.08\)/);
  assert.match(rule, /filter: drop-shadow\([^;]*var\(--accent\)/);
});

test('motion "none" drops the ray ring, hovered or not', () => {
  assert.match(block(HOVER, ':root[data-motion="none"] .app-logo-wrap::before'), /content: none/);
});

test('the webcore skin keeps the glow on hover, and the pixel art never scales', () => {
  const skin = read('webcore/icons.css');
  const rules = [...skin.matchAll(/([^{}]+)\{([^}]*)\}/g)].map(([, sel, body]) => [sel.trim(), body]);
  const stops = rules.filter(([, body]) => /filter:\s*none !important/.test(body));
  assert.ok(stops.length > 0);
  for (const [sel] of stops) {
    for (const s of sel.split(',').map((x) => x.trim()).filter((x) => /\.app-logo$/.test(x))) {
      assert.match(s, /\.app-logo-wrap:not\(\.logo-hover\) \.app-logo$/, `${s} would block the hover glow`);
    }
  }
  assert.match(skin, /\.app-logo-wrap\.logo-hover \.app-logo \{ transform: none !important; \}/, 'the pixel art stays still');
});
