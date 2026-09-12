// Reads the theme tokens straight out of css/theme.css, so themeTokens.json can be pinned
// against the stylesheet that declares them (themeTokens.test.js). Builtins only.

// The custom properties declared in one rule body, in source order.
function declarations(body) {
  const out = {};
  let buf = '';
  let depth = 0;
  let quote = null;
  const push = (s) => {
    const m = s.match(/^\s*(--[\w-]+)\s*:\s*([\s\S]*?)\s*$/);
    if (m) out[m[1]] = m[2];
  };
  for (const ch of body) {
    if (quote) { buf += ch; if (ch === quote) quote = null; continue; }
    if (ch === '"' || ch === "'") { quote = ch; buf += ch; continue; }
    if (ch === '(') depth++;
    else if (ch === ')') depth--;
    else if (ch === ';' && depth === 0) { push(buf); buf = ''; continue; }
    buf += ch;
  }
  push(buf);
  return out;
}

// The body of the first `<selector> {` rule, brace-balanced.
function ruleBody(css, selector) {
  const at = css.indexOf(selector + ' {');
  if (at < 0) throw new Error(`theme.css has no "${selector}" rule`);
  const open = css.indexOf('{', at);
  let depth = 0;
  for (let i = open; i < css.length; i++) {
    if (css[i] === '{') depth++;
    else if (css[i] === '}' && --depth === 0) return css.slice(open + 1, i);
  }
  throw new Error(`theme.css "${selector}" rule is unbalanced`);
}

// A value the browser computes rather than stores: it names another token.
const isDerived = (v) => v.includes('var(') || v.includes('color-mix(');

// theme.css → { tokens, derived }, each token name → { light, dark }. `:root` is light,
// `[data-theme="dark"]` overrides it; a token the dark block omits keeps its light value,
// exactly as the cascade gives it.
export function splitThemeTokens(css) {
  const source = css.replace(/\/\*[\s\S]*?\*\//g, '');
  const light = declarations(ruleBody(source, ':root'));
  const dark = declarations(ruleBody(source, '[data-theme="dark"]'));
  const tokens = {};
  const derived = {};
  for (const [name, value] of Object.entries(light)) {
    const pair = { light: value, dark: name in dark ? dark[name] : value };
    (isDerived(pair.light) || isDerived(pair.dark) ? derived : tokens)[name] = pair;
  }
  return { tokens, derived, light, dark };
}
