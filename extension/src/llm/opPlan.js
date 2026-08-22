// ── Op-plan: extension profile (llm-contract.md §1–§4 + §8 + §13) ──────
// Pure module — no DOM, no chrome, no fetch. The extension's op set (contract §8)
// references images by index in the context listing; `open.actions` carries the §2
// core ops, validated with the same rules as browser/js/llm/opPlan.js (mirrored here,
// since the extension can't import from browser/ — keep them in sync). LLM output is
// data, not instructions: plans are strictly validated before anything executes.

// System prompt (contract §4 two-part rule + §13): the PROSE CORE is data —
// src/config/systemPrompt.json (MV3 ships self-contained; drift-guarded against the
// browser copy by tests/dataParity.test.js). The "Available ops" section is GENERATED
// from OP_REGISTRY further down, so the prompt can never promise an op this surface
// does not register. Clients may append a short dynamic suffix, never prepend anything.
import PROMPT_ASSET from '../config/systemPrompt.json' with { type: 'json' };

const PROMPT_CORE_HEAD = PROMPT_ASSET.extensionHead;
// Prose core, part two — everything after the ops list (the ask paragraph, outlining
// anatomy, colour rules, the chat-only fallback and the injection guard).
const PROMPT_CORE_TAIL = PROMPT_ASSET.extensionTail;

// Limits — the same numbers in every client (contract §1) + the §8 attach/pin caps.
export const LIMITS = {
  actions: 16, variants: 8, layoutLines: 200, stringChars: 5000, attachIndices: 8, pinIndices: 8,
  // §8 panel-settings ops: a search box, the format pills, and a pixel bound.
  filterSearch: 200, filterFormats: 40, filterSize: 100000,
};

// The panel's own theme choice (Options offers exactly these three).
export const THEME_MODES = new Set(['light', 'dark', 'system']);
// open's destination modes (§8): resume an already-open editor tab, or open fresh.
export const OPEN_MODES = new Set(['resume', 'copy']);
// The "elements to include" tick-boxes, by the names the model uses.
export const FILTER_KINDS = new Set(['images', 'css', 'video', 'posters', 'meta']);

// §11 interactive replies — the same numbers as every other client.
export const ASK_LIMITS = { minOptions: 2, maxOptions: 5, question: 300, label: 80, answer: 500 };
export const DEFAULT_CUSTOM_LABEL = 'Something else…';

const HEX = /^#[0-9a-fA-F]{6}$/;
const CROP_TOKEN = /^-?(\d+(\.\d+)?|\.\d+)(%|px|cm|in)?$/;
const PAGE_FORMAT = /^[abc](10|[0-9])$/;   // lowercase ISO names a0…c10
const STYLES = new Set(['solid', 'dashed', 'dotted']);
const FILTERS = new Set(['none', 'bw', 'sepia', 'invert', 'contour', 'custom']);
const LINE_KEYS = new Set(['points', 'color', 'thickness', 'pointSize', 'style', 'locked', 'fillColor']);

// The full §2 core-op set: at the TOP level of an extension plan these are dropped
// with a warning (contract §8 — editing happens in the editor after `open`).
const CORE_OPS = new Set(['crop', 'rotate', 'filter', 'layout', 'formula', 'page', 'blank', 'frame']);
// The subset allowed inside open.actions (contract §8).
const OPEN_OPS = new Set(['crop', 'rotate', 'filter', 'layout', 'page']);

const isObj = (v) => v != null && typeof v === 'object' && !Array.isArray(v);
const isStr = (v, max = LIMITS.stringChars) => typeof v === 'string' && v.length <= max;
const fail = (op, why) => { throw new Error(`Invalid ${op} action: ${why}`); };

// Reject unknown fields on a KNOWN op — that fails the whole plan (contract §1).
const onlyKeys = (a, allowed) => {
  const extra = Object.keys(a).filter((k) => k !== 'op' && !allowed.includes(k));
  if (extra.length) fail(a.op, `unknown field "${extra[0]}"`);
};

// ── §2 core-op validators (mirror of browser/js/llm/opPlan.js — keep in sync) ──
const CORE_VALIDATORS = {
  crop(a) {
    onlyKeys(a, ['spec']);
    if (!isObj(a.spec)) fail('crop', '"spec" must be an object');
    const keys = Object.keys(a.spec);
    const bad = keys.find((k) => !['x1', 'x2', 'y1', 'y2'].includes(k));
    if (bad) fail('crop', `unknown spec key "${bad}"`);
    if (!keys.length) fail('crop', 'spec needs at least one of x1/x2/y1/y2');
    const spec = {};
    for (const k of keys) {
      const tok = a.spec[k];
      if (!isStr(tok) || !CROP_TOKEN.test(tok)) fail('crop', `bad token for "${k}"`);
      spec[k] = tok;
    }
    return { op: 'crop', spec };
  },
  rotate(a) {
    onlyKeys(a, ['dir', 'times']);
    if (a.dir !== 'left' && a.dir !== 'right') fail('rotate', '"dir" must be "left" or "right"');
    const times = a.times == null ? 1 : a.times;
    if (!Number.isInteger(times) || times < 1 || times > 3) fail('rotate', '"times" must be an integer 1..3');
    return { op: 'rotate', dir: a.dir, times };
  },
  filter(a) {
    onlyKeys(a, ['mode', 'tint']);
    if (!FILTERS.has(a.mode)) fail('filter', `unknown mode "${a.mode}"`);
    if (a.mode === 'custom') {
      if (!isStr(a.tint) || !HEX.test(a.tint)) fail('filter', '"custom" requires "tint" as #rrggbb');
      return { op: 'filter', mode: 'custom', tint: a.tint };
    }
    if (a.tint != null) fail('filter', '"tint" is only valid with mode "custom"');
    return { op: 'filter', mode: a.mode };
  },
  layout(a) {
    onlyKeys(a, ['lines']);
    if (!Array.isArray(a.lines)) fail('layout', '"lines" must be an array');
    if (a.lines.length > LIMITS.layoutLines) fail('layout', `more than ${LIMITS.layoutLines} lines`);
    const lines = a.lines.map((l) => {
      if (!isObj(l)) fail('layout', 'each line must be an object');
      const bad = Object.keys(l).find((k) => !LINE_KEYS.has(k));
      if (bad) fail('layout', `unknown line field "${bad}"`);
      if (!Array.isArray(l.points)) fail('layout', 'line "points" must be an array');
      const out = {
        points: l.points.map((p) => {
          if (!isObj(p) || Object.keys(p).some((k) => k !== 'x' && k !== 'y')) fail('layout', 'points must be {x, y} objects');
          if (!Number.isFinite(p.x) || !Number.isFinite(p.y)) fail('layout', 'point coords must be finite numbers');
          return { x: p.x, y: p.y };
        }),
      };
      if (l.color != null) { if (!isStr(l.color)) fail('layout', 'line "color" must be a string'); out.color = l.color; }
      if (l.thickness != null) { if (!Number.isFinite(l.thickness)) fail('layout', 'line "thickness" must be a number'); out.thickness = l.thickness; }
      if (l.pointSize != null) { if (!Number.isFinite(l.pointSize)) fail('layout', 'line "pointSize" must be a number'); out.pointSize = l.pointSize; }
      if (l.style != null) { if (!STYLES.has(l.style)) fail('layout', `unknown line style "${l.style}"`); out.style = l.style; }
      if (l.locked != null) { if (typeof l.locked !== 'boolean') fail('layout', 'line "locked" must be a boolean'); out.locked = l.locked; }
      if (l.fillColor != null) { if (!isStr(l.fillColor)) fail('layout', 'line "fillColor" must be a string'); out.fillColor = l.fillColor; }
      return out;
    });
    return { op: 'layout', lines };
  },
  page(a) {
    onlyKeys(a, ['format']);
    if (!isStr(a.format) || !PAGE_FORMAT.test(a.format)) fail('page', '"format" must be a lowercase ISO name a0–a10, b0–b10 or c0–c10');
    return { op: 'page', format: a.format };
  },
};

// An image index must point into the current context listing.
const validIndex = (v, listingLength, op) => {
  if (!Number.isInteger(v)) fail(op, '"image" indices must be integers');
  if (v < 0 || v >= listingLength) fail(op, `image index ${v} is out of range (the listing has ${listingLength} entries)`);
  return v;
};

// The attach/pin/unpin shape: exactly one of `image` / `images` (≤ 8), normalized
// to a bounded `indices` array.
const indicesOp = (op, a, listingLength, cap = LIMITS.pinIndices) => {
  onlyKeys(a, ['image', 'images']);
  const hasImage = a.image != null, hasImages = a.images != null;
  if (hasImage === hasImages) fail(op, 'exactly one of "image" / "images" is required');
  if (hasImage) return { op, indices: [validIndex(a.image, listingLength, op)] };
  if (!Array.isArray(a.images) || !a.images.length) fail(op, '"images" must be a non-empty array');
  if (a.images.length > cap) fail(op, `more than ${cap} images`);
  return { op, indices: a.images.map((i) => validIndex(i, listingLength, op)) };
};

// The actions allowed inside `open` (§2 subset). A §2 op OUTSIDE the subset is a
// known-but-invalid action → the whole plan fails; a truly unknown op drops with a
// warning (§1 forward compatibility).
const validateOpenActions = (list, warnings) => {
  if (list == null) return [];
  if (!Array.isArray(list)) fail('open', '"actions" must be an array');
  if (list.length > LIMITS.actions) fail('open', `more than ${LIMITS.actions} actions`);
  const out = [];
  for (const a of list) {
    if (!isObj(a) || typeof a.op !== 'string') fail('open', 'every entry in "actions" must be an object with an "op"');
    if (OPEN_OPS.has(a.op)) { out.push(CORE_VALIDATORS[a.op](a)); continue; }
    if (CORE_OPS.has(a.op)) fail('open', `"${a.op}" is not allowed in open.actions (only crop/rotate/filter/layout/page)`);
    warnings.push(`Skipped unknown operation "${a.op}" inside "open"`);
  }
  return out;
};

// ── The op registry (contract §8 + §13) ──
// The single source of an op's existence on this surface: validator + prompt bullet +
// flags in ONE ordered entry (chatController.js's executors key on the same names; the
// prompt's "Available ops" is assembled from the bullets in this order). `gather` marks
// context-gathering ops (§8 auto-continuation), `panelSettings` the panel's own controls.
export const OP_REGISTRY = [
  {
    name: 'focus',
    bullet: `- {"op":"focus","image":3} — scroll image 3 into view on the page and highlight it.`,
    validate(a, listingLength) {
      onlyKeys(a, ['image']);
      return { op: 'focus', image: validIndex(a.image, listingLength, 'focus') };
    },
  },
  {
    name: 'open',
    bullet: `- {"op":"open","image":3,"mode":"copy","actions":[...],"incognito":false} — open image 3 in
  the Stencil editor. "mode":"resume" focuses the editor tab ALREADY holding that image
  instead of opening a fresh copy — prefer it when the image was opened before (it is the
  non-destructive path); "copy" (the default) always opens fresh. "actions" is an optional
  list of editor operations applied on open, drawn from
  the core op set crop / rotate / filter / layout / page:
  - {"op":"crop","spec":{"x1":"10%","x2":"-10%","y1":"0","y2":"90%"}} — move edges inward;
    tokens are numbers with optional unit % / px / cm / in; a leading "-" measures from the
    opposite side. Include only the edges you want to move.
  - {"op":"rotate","dir":"left"|"right","times":1..3} — quarter turns only.
  - {"op":"filter","mode":"none"|"bw"|"sepia"|"invert"|"contour"|"custom","tint":"#rrggbb"}
    — "custom" is a duotone tint and requires "tint"; "contour" is edge detection.
  - {"op":"layout","lines":[{"points":[{"x":0,"y":0},...],"color":"#FFFF00","thickness":2,
    "pointSize":4,"style":"solid"|"dashed"|"dotted","locked":false,"fillColor":"transparent"}]}
    — draw annotation polylines in image-pixel coordinates.
  - {"op":"page","format":"a4"} — ISO page formats a0–a10, b0–b10, c0–c10.`,
    validate(a, listingLength, warnings) {
      onlyKeys(a, ['image', 'actions', 'incognito', 'mode']);
      if (a.incognito != null && typeof a.incognito !== 'boolean') fail('open', '"incognito" must be a boolean');
      if (a.mode != null && !OPEN_MODES.has(a.mode)) fail('open', '"mode" must be "resume" or "copy"');
      const out = {
        op: 'open',
        image: validIndex(a.image, listingLength, 'open'),
        actions: validateOpenActions(a.actions, warnings),
      };
      if (a.incognito != null) out.incognito = a.incognito;
      if (a.mode != null) out.mode = a.mode;
      return out;
    },
  },
  {
    name: 'attach',
    gather: true,
    bullet: `- {"op":"attach","image":3} or {"op":"attach","images":[0,1,2]} — fetch the image(s) and
  attach them to this conversation so you can look at them (max 8); use this when you must
  see the images to answer (e.g. "which of these is the cat?", "read the chart").`,
    validate(a, listingLength) {
      return indicesOp('attach', a, listingLength, LIMITS.attachIndices);
    },
  },
  {
    name: 'pin',
    bullet: `- {"op":"pin","image":3} or {"op":"pin","images":[0,1,2]} — pin the image(s) in the Stencil
  panel (max 8 per action) so they float to the top of the list; use this when the user asks
  to pin, keep, or shortlist images.`,
    validate(a, listingLength) {
      return indicesOp('pin', a, listingLength);
    },
  },
  {
    // The missing other half of pin (§8 panel-op widening) — local pins only, and the
    // exact same index shape and cap.
    name: 'unpin',
    bullet: `- {"op":"unpin","image":3} or {"op":"unpin","images":[0,1,2]} — unpin image(s) pinned in the
  Stencil panel (max 8 per action; local pins only); use this when the user asks to unpin or
  drop images from their shortlist.`,
    validate(a, listingLength) {
      return indicesOp('unpin', a, listingLength);
    },
  },
  {
    name: 'scanTab',
    gather: true,
    bullet: `- {"op":"scanTab","tab":2} — scan open browser tab 2 (from the tab list in the context
  below) and REPLACE this conversation's image listing with that tab's images; use this when
  the user asks about a page they have open in another tab. After the switch, indices refer
  to the new listing.`,
    validate(a, listingLength, warnings, tabsLength) {
      onlyKeys(a, ['tab']);
      if (!Number.isInteger(a.tab)) fail('scanTab', '"tab" must be an integer');
      if (!tabsLength) fail('scanTab', 'there are no other open tabs to scan');
      if (a.tab < 0 || a.tab >= tabsLength) fail('scanTab', `tab index ${a.tab} is out of range (the tab list has ${tabsLength} entries)`);
      return { op: 'scanTab', tab: a.tab };
    },
  },
  {
    // Re-scan the conversation's CURRENT page (§8 panel-op widening) — the refresh
    // scanTab cannot express. A GATHER op: it joins the auto-continuation.
    name: 'rescan',
    gather: true,
    bullet: `- {"op":"rescan"} — re-scan the CURRENT page and refresh this conversation's image listing;
  use this when the page may have changed (images loaded since the scan) or the user asks to
  refresh or rescan. After it, indices refer to the fresh listing.`,
    validate(a) {
      onlyKeys(a, []);
      return { op: 'rescan' };
    },
  },
  {
    name: 'openUrl',
    bullet: `- {"op":"openUrl","url":"https://…","incognito":false} — open an image URL in the Stencil
  editor ("incognito": true opens it as an incognito editor). ONLY a URL the user themselves
  wrote in this conversation — never introduce, complete, or rewrite one; images from the
  page listing open with "open" by index instead.`,
    validate(a) {
      onlyKeys(a, ['url', 'incognito']);
      if (!isStr(a.url) || !/^https?:\/\/\S+$/i.test(a.url.trim())) fail('openUrl', '"url" must be an http(s) URL');
      if (a.incognito != null && typeof a.incognito !== 'boolean') fail('openUrl', '"incognito" must be a boolean');
      const out = { op: 'openUrl', url: a.url.trim() };
      if (a.incognito != null) out.incognito = a.incognito;
      return out;
    },
  },
  {
    // Panel settings (contract §8). These touch the extension's OWN view — no page
    // access, no network — so they carry no image indices and cannot fail a listing.
    name: 'theme',
    panelSettings: true,
    bullet: `- {"op":"theme","mode":"dark"|"light"|"system"} — switch the Stencil panel's own theme
  ("system" follows the OS setting). This changes the panel's appearance only.`,
    validate(a) {
      onlyKeys(a, ['mode']);
      if (!isStr(a.mode) || !THEME_MODES.has(a.mode)) {
        fail('theme', `"mode" must be one of ${[...THEME_MODES].join(', ')}`);
      }
      return { op: 'theme', mode: a.mode };
    },
  },
  {
    // The panel accent (§8 panel-op widening, §10's shape): a "#rrggbb" hex, OR a named
    // preset — exactly one. The executor resolves the hex onto the panel's preset store.
    name: 'accent',
    panelSettings: true,
    bullet: `- {"op":"accent","color":"#7c3aed"} — set the Stencil panel's own accent colour. "color"
  must be # + 6 hex digits (the nearest of the panel's preset accents applies); also
  accepts {"op":"accent","preset":"green"} — a preset name (violet, pink, yellow, orange,
  crimson, aqua, sky, blue, grass, green, brown, grey). Panel appearance only.`,
    validate(a) {
      onlyKeys(a, ['color', 'preset']);
      const hasColor = a.color != null, hasPreset = a.preset != null;
      if (hasColor === hasPreset) fail('accent', 'exactly one of "color" / "preset" is required');
      if (hasColor) {
        if (!isStr(a.color) || !HEX.test(a.color)) fail('accent', '"color" must be #rrggbb');
        return { op: 'accent', color: a.color };
      }
      if (!isStr(a.preset, 40) || !a.preset.trim()) fail('accent', '"preset" must be a short accent name');
      return { op: 'accent', preset: a.preset.trim() };
    },
  },
  {
    name: 'filter',
    panelSettings: true,
    bullet: `- {"op":"filter","search":"cat","regex":false,"kinds":["images","css","video","posters",
  "meta"],"formats":["PNG","JPG"],"minWidth":200,"maxWidth":0,"minHeight":0,"maxHeight":0,
  "markOpened":true,"openedFirst":true,"showPinned":true}
  — set the panel's own filters, i.e. what the list shows. Every field is optional; send
  only the ones you are changing. "kinds" replaces the "elements to include" tick-boxes,
  "formats" the format pills (["*"] selects every format, [] clears them all), and a size
  bound of 0 clears that bound. The booleans drive the three list toggles: "markOpened"
  badges already-opened images, "openedFirst" sorts them first, "showPinned" floats pinned
  images to the top. Use this when the user asks to show, hide, narrow, search
  or filter the listing — never to answer a question about the images yourself.`,
    validate(a) {
      onlyKeys(a, ['search', 'regex', 'kinds', 'formats',
                   'minWidth', 'maxWidth', 'minHeight', 'maxHeight',
                   'markOpened', 'openedFirst', 'showPinned']);
      const out = { op: 'filter' };
      if (a.search != null) {
        if (!isStr(a.search)) fail('filter', '"search" must be a string');
        if (a.search.length > LIMITS.filterSearch) fail('filter', `"search" is longer than ${LIMITS.filterSearch} characters`);
        out.search = a.search;
      }
      if (a.regex != null) {
        if (typeof a.regex !== 'boolean') fail('filter', '"regex" must be a boolean');
        out.regex = a.regex;
      }
      if (a.kinds != null) {
        if (!Array.isArray(a.kinds)) fail('filter', '"kinds" must be an array');
        for (const k of a.kinds) {
          if (!isStr(k) || !FILTER_KINDS.has(k)) {
            fail('filter', `"kinds" entries must be one of ${[...FILTER_KINDS].join(', ')}`);
          }
        }
        out.kinds = [...new Set(a.kinds)];
      }
      if (a.formats != null) {
        if (!Array.isArray(a.formats)) fail('filter', '"formats" must be an array');
        if (a.formats.length > LIMITS.filterFormats) fail('filter', `more than ${LIMITS.filterFormats} formats`);
        for (const f of a.formats) if (!isStr(f)) fail('filter', '"formats" entries must be strings');
        // Upper-cased here so the executor can match the panel's own pill labels.
        out.formats = a.formats.map((f) => f.trim().toUpperCase()).filter(Boolean);
      }
      for (const key of ['minWidth', 'maxWidth', 'minHeight', 'maxHeight']) {
        if (a[key] == null) continue;
        if (!Number.isInteger(a[key]) || a[key] < 0) fail('filter', `"${key}" must be an integer >= 0`);
        if (a[key] > LIMITS.filterSize) fail('filter', `"${key}" is larger than ${LIMITS.filterSize}`);
        out[key] = a[key];
      }
      // The three list toggles (§8 panel-op widening): badge already-opened images,
      // sort them first, float pinned images.
      for (const key of ['markOpened', 'openedFirst', 'showPinned']) {
        if (a[key] == null) continue;
        if (typeof a[key] !== 'boolean') fail('filter', `"${key}" must be a boolean`);
        out[key] = a[key];
      }
      if (Object.keys(out).length === 1) fail('filter', 'at least one filter field is required');
      return out;
    },
  },
  {
    // §10 clearChat, carried into this profile as a panel op: no page access, no
    // indices, no gather. DEFERRED — the controller runs it after the turn's other
    // actions and any continuation round, behind the panel's own confirm.
    name: 'clearChat',
    panelSettings: true,
    bullet: `- {"op":"clearChat"} — clear THIS conversation's history; the app asks the user to
  confirm first, and the clear happens after this plan's other actions finish. This IS
  what "clear the chat / conversation / history" means; never answer that it cannot be
  done. Takes no fields.`,
    validate(a) {
      onlyKeys(a, []);
      return { op: 'clearChat' };
    },
  },
];

// Validator lookup for parseOpPlan, derived from the registry.
const EXT_VALIDATORS = Object.fromEntries(OP_REGISTRY.map((e) => [e.name, e.validate]));
// The gather set (§8 auto-continuation), likewise derived.
const GATHER_OPS = new Set(OP_REGISTRY.filter((e) => e.gather).map((e) => e.name));

// ── §13 forbidden ops ──
// Never model-drivable, on any surface: llm/provider configuration, clipboard reads,
// hotkey rebinding, session end, chat persistence/consent and server-side destruction —
// plus the §8 un-drivable extension surface (editor URL, Options, shareTabs, downloads).
// Teeth: a test pins that no registry entry uses these names, and chatController
// refuses them at execution even if one somehow appears.
export const FORBIDDEN_OPS = new Set([
  'llm', 'llmSettings', 'provider', 'model', 'apiKey', 'baseUrl',
  'paste',
  'hotkey', 'hotkeys', 'rebind',
  'quit', 'exit', 'closeWindow', 'endSession',
  'chatPersist', 'chatConsent', 'persistChat',
  'deleteProject', 'deleteServerProject', 'expireProject',
  'editorUrl', 'options', 'shareTabs', 'download',
]);

// ── §13 prompt generation ──
// A registry bullet must never smuggle credentials or endpoint-setting text into the
// prompt: assembly fails loudly on these patterns instead of leaking. (The bare word
// "tokens" is fine — crop's unit tokens — so the token pattern is auth-qualified.)
const SENSITIVE_BULLET_PATTERNS = [
  /api[\s_-]?key/i,
  /\bbearer\b/i,
  /(auth|access|secret|session)[\s_-]?token/i,
  /\bendpoint\b/i,
];
const censorBullet = (name, bullet) => {
  for (const re of SENSITIVE_BULLET_PATTERNS) {
    if (re.test(bullet)) throw new Error(`Refusing to emit the prompt bullet for "${name}": it matches the sensitive pattern ${re}`);
  }
  return bullet;
};

// Assemble the prompt from the prose core + the registry's bullets, in order. §13
// capability truth: `exclude` names ops whose runtime capability is not wired on this
// surface — their bullets are omitted, so the model is never promised them. The shipped
// extension wires every registered op, so LLM_SYSTEM_PROMPT excludes nothing.
export const buildSystemPrompt = (registry = OP_REGISTRY, { exclude = new Set() } = {}) => {
  const ops = registry.filter((e) => !exclude.has(e.name))
    .map((e) => censorBullet(e.name, e.bullet)).join('\n');
  return `${PROMPT_CORE_HEAD}\n${ops}\n\n${PROMPT_CORE_TAIL}`;
};

export const LLM_SYSTEM_PROMPT = buildSystemPrompt();

// Take the first balanced { … } object (string-aware) from the text, or null.
const firstJsonObject = (text) => {
  const start = text.indexOf('{');
  if (start < 0) return null;
  let depth = 0, inStr = false, esc = false;
  for (let i = start; i < text.length; i++) {
    const c = text[i];
    if (inStr) {
      if (esc) esc = false;
      else if (c === '\\') esc = true;
      else if (c === '"') inStr = false;
      continue;
    }
    if (c === '"') inStr = true;
    else if (c === '{') depth++;
    else if (c === '}') { depth--; if (depth === 0) return text.slice(start, i + 1); }
  }
  return null;
};

// Parse the raw LLM reply into a validated extension plan { reply, actions, warnings,
// chatOnly } — see parseOpPlan below. Shared §1 mechanics: Markdown fences stripped,
// first balanced JSON object wins, no JSON at all = a chat-only turn (not an error).
// Extension rules (§8): §2 core ops at the top level DROP with a warning, and so do
// "variants" (§1's leniency clause). Invalid plans THROW.
// ── §11 interactive replies (`ask`) ─────────────────────────────────────────
// A question put back to the user as a choice card, validated as strictly as an action.
// The extension is not an editor, so an option's PREVIEW can only be an existing image
// (`image.scanIndex`); an option carrying preview `actions` or a model-supplied
// `image.url` keeps its label but loses the picture, with a warning (§11.2).
const ASK_KEYS = new Set(['question', 'mode', 'options', 'allowCustom', 'customLabel']);
const OPTION_KEYS = new Set(['label', 'actions', 'image']);
const IMAGE_KEYS = ['url', 'projectId', 'scanIndex'];

const validateAskImage = (img, where, listingLength, warnings) => {
  if (!isObj(img)) throw new Error(`Invalid plan: ${where} "image" must be an object`);
  const unknown = Object.keys(img).filter((k) => !IMAGE_KEYS.includes(k));
  if (unknown.length) throw new Error(`Invalid plan: ${where} "image" has unknown field "${unknown[0]}"`);
  const given = IMAGE_KEYS.filter((k) => img[k] != null);
  if (given.length !== 1) throw new Error(`Invalid plan: ${where} "image" needs exactly one of ${IMAGE_KEYS.join(', ')}`);
  const [key] = given;
  if (key === 'scanIndex') {
    if (!Number.isInteger(img.scanIndex) || img.scanIndex < 0) throw new Error(`Invalid plan: ${where} "image.scanIndex" must be an integer >= 0`);
    // An index past the listing is the model mis-numbering: keep the option, lose its picture.
    if (listingLength && img.scanIndex >= listingLength) {
      warnings.push(`${where}: image ${img.scanIndex} is not in the list — the option is shown without a preview`);
      return null;
    }
    return { scanIndex: img.scanIndex };
  }
  if (!isStr(img[key]) || !img[key].trim()) throw new Error(`Invalid plan: ${where} "image.${key}" must be a non-empty string`);
  // Never resolved (§11.2): loading it would make the popup — which holds <all_urls>
  // and the user's cookies — fetch a model-chosen host. Option kept, picture dropped.
  if (key === 'url') {
    warnings.push(`${where}: an image URL from the model is not fetched here — the option is shown without a preview`);
    return null;
  }
  return { [key]: img[key].trim() };
};

const validateAskOption = (opt, i, listingLength, warnings) => {
  const where = `ask option ${i + 1}`;
  if (!isObj(opt)) throw new Error(`Invalid plan: ${where} must be an object`);
  const unknown = Object.keys(opt).filter((k) => !OPTION_KEYS.has(k));
  if (unknown.length) throw new Error(`Invalid plan: ${where} has unknown field "${unknown[0]}"`);
  if (!isStr(opt.label) || !opt.label.trim()) throw new Error(`Invalid plan: ${where} "label" must be a non-empty string`);
  if (opt.label.length > ASK_LIMITS.label) throw new Error(`Invalid plan: ${where} "label" is longer than ${ASK_LIMITS.label} characters`);
  if (opt.actions != null && opt.image != null) throw new Error(`Invalid plan: ${where} carries both "actions" and "image" — an option previews a render OR names an existing image`);
  const out = { label: opt.label.trim() };
  if (opt.actions != null) {
    if (!Array.isArray(opt.actions)) throw new Error(`Invalid plan: ${where} "actions" must be an array`);
    // No working image in the extension: the option stands, its preview doesn't.
    warnings.push(`${where}: this surface can't render a preview of an edit — the option is shown without one`);
  }
  if (opt.image != null) {
    const image = validateAskImage(opt.image, where, listingLength, warnings);
    if (image) out.image = image;
  }
  return out;
};

// Validate the optional `ask` object → the normalised card, or null when absent.
export const validateAsk = (ask, listingLength, warnings) => {
  if (ask == null) return null;
  if (!isObj(ask)) throw new Error('Invalid plan: "ask" must be an object');
  const unknown = Object.keys(ask).filter((k) => !ASK_KEYS.has(k));
  if (unknown.length) throw new Error(`Invalid plan: "ask" has unknown field "${unknown[0]}"`);
  if (!isStr(ask.question) || !ask.question.trim()) throw new Error('Invalid plan: "ask.question" must be a non-empty string');
  if (ask.question.length > ASK_LIMITS.question) throw new Error(`Invalid plan: "ask.question" is longer than ${ASK_LIMITS.question} characters`);
  const mode = ask.mode == null ? 'single' : ask.mode;
  if (mode !== 'single' && mode !== 'multi') throw new Error('Invalid plan: "ask.mode" must be "single" or "multi"');
  if (!Array.isArray(ask.options)) throw new Error('Invalid plan: "ask.options" must be an array');
  if (ask.options.length < ASK_LIMITS.minOptions || ask.options.length > ASK_LIMITS.maxOptions)
    throw new Error(`Invalid plan: "ask.options" must hold ${ASK_LIMITS.minOptions}..${ASK_LIMITS.maxOptions} options`);
  if (ask.allowCustom != null && typeof ask.allowCustom !== 'boolean') throw new Error('Invalid plan: "ask.allowCustom" must be a boolean');
  if (ask.customLabel != null) {
    if (!isStr(ask.customLabel) || !ask.customLabel.trim()) throw new Error('Invalid plan: "ask.customLabel" must be a non-empty string');
    if (ask.customLabel.length > ASK_LIMITS.label) throw new Error(`Invalid plan: "ask.customLabel" is longer than ${ASK_LIMITS.label} characters`);
  }
  return {
    question: ask.question.trim(),
    mode,
    allowCustom: ask.allowCustom === true,
    customLabel: (ask.customLabel && ask.customLabel.trim()) || DEFAULT_CUSTOM_LABEL,
    options: ask.options.map((o, i) => validateAskOption(o, i, listingLength, warnings)),
  };
};

// The text an answered card sends as the user's next turn.
export const askAnswerText = (ask, { picked = [], custom = '' } = {}) => {
  const typed = String(custom || '').trim();
  if (typed) return typed.slice(0, ASK_LIMITS.answer);
  return (Array.isArray(picked) ? picked : [picked])
    .map((p) => (isObj(p) ? p.label : p)).filter((l) => isStr(l) && l.trim())
    .join(', ').slice(0, ASK_LIMITS.answer);
};

export const parseOpPlan = (text, { listingLength = 0, tabsLength = 0 } = {}) => {
  const raw = String(text == null ? '' : text);
  const chatOnly = () => ({ reply: raw.trim(), actions: [], ask: null, warnings: [], chatOnly: true });
  const candidate = firstJsonObject(raw.replace(/```[a-zA-Z]*/g, ''));
  if (candidate == null) return chatOnly();
  let obj;
  try { obj = JSON.parse(candidate); } catch { return chatOnly(); }   // not actually JSON → chat-only

  // `version` other than 1 (or absent) is accepted but ignored.
  // §1 reply tolerance: models routinely omit the reply while planning valid
  // actions — substitute rather than lose the plan to a missing pleasantry.
  let reply = obj.reply;
  const replyOmitted = typeof reply !== 'string' || !reply.trim();
  if (obj.variants != null && !Array.isArray(obj.variants)) throw new Error('Invalid plan: "variants" must be an array');

  const warnings = [];
  // §1's leniency clause, applied to §8's "variants must be empty": the extension
  // has no variants to render, but losing the whole turn to a misplaced one costs
  // the user everything — drop them with a warning and run the rest.
  const droppedVariants = Array.isArray(obj.variants) ? obj.variants.length : 0;
  if (droppedVariants) {
    warnings.push(`Skipped ${droppedVariants} variant${droppedVariants === 1 ? '' : 's'} — the extension doesn't edit images, so it renders no variants; the rest of the plan ran`);
  }

  const list = obj.actions == null ? [] : obj.actions;
  if (!Array.isArray(list)) throw new Error('Invalid plan: "actions" must be an array');
  if (list.length > LIMITS.actions) throw new Error(`Invalid plan: more than ${LIMITS.actions} actions`);
  const actions = [];
  for (const a of list) {
    if (!isObj(a) || typeof a.op !== 'string') throw new Error('Invalid plan: every action must be an object with an "op"');
    const validate = EXT_VALIDATORS[a.op];
    if (validate) { actions.push(validate(a, listingLength, warnings, tabsLength)); continue; }
    // Core ops at the top level drop with a warning (§8); truly unknown ops drop
    // with the generic §1 forward-compatibility warning.
    if (CORE_OPS.has(a.op)) warnings.push(`Skipped core operation "${a.op}" — the extension doesn't edit images; use it inside an "open" action`);
    else warnings.push(`Skipped unknown operation "${a.op}"`);
  }
  const ask = validateAsk(obj.ask, listingLength, warnings);
  // The substitute must not overstate what happened: "Done." only when the plan
  // actually carries work — an empty plan says so, since a bare "Done." there
  // reads as a success that never occurred.
  if (replyOmitted) {
    if (actions.length || ask) {
      reply = 'Done.';
      warnings.push('The model omitted its reply — the plan still ran');
    } else {
      reply = 'The model returned an empty plan — nothing was changed.';
    }
  }
  return { reply, actions, ask, warnings, chatOnly: false };
};

// True when the plan's only actionable output is `attach` — the trigger for the
// single bounded auto-continuation (contract §8).
export const attachOnly = (plan) =>
  !!plan && Array.isArray(plan.actions) && plan.actions.length > 0
  && plan.actions.every((a) => a.op === 'attach');

// True when every action only GATHERS context (the registry's `gather` flag: attach /
// scanTab / rescan) — the plan needs another model round to act on what arrived, so it
// triggers the same single bounded auto-continuation as attach-only plans (contract §8).
export const continuationOnly = (plan) =>
  !!plan && Array.isArray(plan.actions) && plan.actions.length > 0
  && plan.actions.every((a) => GATHER_OPS.has(a.op));
