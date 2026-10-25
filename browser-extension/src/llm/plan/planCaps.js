// ── The §1 JSON caps over a parsed op plan (llm-contract.md §1) ──
// Pure. The JS twin of core/json/jsonReader's caps (limits.json in opRegistry.json), pinned
// by common/fixtures/llm/opPlan/generated/normalized.json.

// UTF-8 bytes of a JS string; a lone surrogate counts the 3 bytes of its U+FFFD.
export const utf8Length = (s) => {
  let n = 0;
  for (let i = 0; i < s.length; i++) {
    const c = s.charCodeAt(i);
    if (c < 0x80) n += 1;
    else if (c < 0x800) n += 2;
    else if (c >= 0xd800 && c <= 0xdbff && i + 1 < s.length && (s.charCodeAt(i + 1) & 0xfc00) === 0xdc00) { n += 4; i++; }
    else n += 3;
  }
  return n;
};

// The §1 JSON caps over a parsed plan: bytes of its text, then nesting depth (the root
// object is 1), then values (every value counts, keys do not). Null when within all three.
export const jsonLimit = (value, text, caps) => {
  if (!caps) return null;
  if (utf8Length(text) > caps.bytes) return { limit: 'MAX_BYTES', max: caps.bytes, detail: `the plan's JSON is larger than ${caps.bytes} bytes` };
  let deepest = 0, nodes = 0;
  const stack = [[value, 1]];
  while (stack.length) {
    const [v, depth] = stack.pop();
    nodes++;
    if (v === null || typeof v !== 'object') continue;
    deepest = Math.max(deepest, depth);
    for (const x of Array.isArray(v) ? v : Object.values(v)) stack.push([x, depth + 1]);
  }
  if (deepest > caps.depth) return { limit: 'MAX_DEPTH', max: caps.depth, detail: `the plan's JSON nests deeper than ${caps.depth} levels` };
  if (nodes > caps.nodes) return { limit: 'MAX_NODES', max: caps.nodes, detail: `the plan's JSON holds more than ${caps.nodes} values` };
  return null;
};
