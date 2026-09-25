// The header mark's hover: it loops under every motion mode, "none" included, and only the
// webcore skin stills it to a grow and an edge glow (logoHover.css).
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

test('motion "none" keeps the main mark\'s loop: the pulse and the rays run at their own beat', () => {
  const pulse = block(HOVER, ':root[data-motion="none"] .app-logo-wrap.logo-hover .app-logo');
  assert.match(pulse, /animation-duration: 1\.2s !important/);
  assert.match(pulse, /animation-iteration-count: infinite !important/);
  const rays = block(HOVER, ':root[data-motion="none"] .app-logo-wrap.logo-hover::before');
  assert.match(rays, /animation-duration: 8s, 1\.2s !important/);
  assert.match(rays, /animation-iteration-count: infinite !important/);
});

test('the webcore skin stills the hovered mark: no loop, a small grow, an accent glow, no rays', () => {
  const rule = block(HOVER, ':root[data-skin="webcore"] .app-logo-wrap.logo-hover .app-logo');
  assert.match(rule, /animation: none !important/);
  assert.match(rule, /transform: scale\(1\.08\) !important/);
  assert.match(rule, /filter: drop-shadow\([^;]*var\(--accent\)/);
  assert.match(block(HOVER, ':root[data-skin="webcore"] .app-logo-wrap::before'), /content: none/);
});

test('the webcore skin keeps the glow on hover', () => {
  const skin = read('webcore/icons.css');
  const rules = [...skin.matchAll(/([^{}]+)\{([^}]*)\}/g)].map(([, sel, body]) => [sel.trim(), body]);
  const stops = rules.filter(([, body]) => /filter:\s*none !important/.test(body));
  assert.ok(stops.length > 0);
  for (const [sel] of stops) {
    for (const s of sel.split(',').map((x) => x.trim()).filter((x) => /\.app-logo$/.test(x))) {
      assert.match(s, /\.app-logo-wrap:not\(\.logo-hover\) \.app-logo$/, `${s} would block the hover glow`);
    }
  }
});
