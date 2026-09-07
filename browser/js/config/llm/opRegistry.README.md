# opRegistry.json — the normative, table-driving op registry

Machine-readable record of the LLM op-plan surface (`llm-contract.md` §1–§13) across all
seven implementations, and the table every one of them **validates from**. A surface's
validator reads this file (embedded: qrc alias on desktop, `@embedFile` on cli,
`include_str!` on mcp, `EmbeddedResource` on bot, a drift-guarded copy on pystencil and the
extension) and keeps only three things of its own: its normalizers (the typed action it
builds), its executors, and the few **native rules** named below. Change a key's type,
range, enum, cap or grammar here and every surface changes with it; the shared fixture
corpus (`fixtures/opPlan/`) is the cross-language proof that they all agree — its mechanical
half (`fixtures/opPlan/generated/cases.json`) is itself derived from this file by
`browser/tools/genOpPlanFixtures.mjs` (`npm run gen-fixtures` after any registry change).

Guarded by `browser/tests/opRegistryCanon.test.js` (pins the file to the live browser
structures and cross-checks the corpus) and by every surface's fixture walker.

## Top-level sections

- **`$meta`** — `schemaVersion` (2), the surface → profile map (browser/desktop →
  `editor`, cli/pystencil → `console`, `bot`, `mcp`, `extension`), sources, guard.
- **`limits`** — the §1 caps (`MAX_ACTIONS`, `MAX_VARIANTS`, `MAX_LAYOUT_LINES`,
  `MAX_FRAME_INDICES`, `MAX_STRING_CHARS`, `MAX_SAVE_NAME`, `MAX_PATH_CHARS`,
  `MAX_RENAME_NAME`, `MAX_UNDO_STEPS`), `ask.*` (§11) and `extension.*` (§8). A key spec
  references one by its dotted name (`"maxItems": "MAX_ACTIONS"`, `"maxChars": "ask.label"`).
- **`regexes`** — the token grammars as **flag-free** regex sources (case-insensitivity is
  spelled out in the pattern, so cli/mcp hand-match them): `CROP_TOKEN`, `CROP_ASPECT`
  (encodes both sides > 0), `PAGE_FORMAT`, `HEX`, `CSS_NAME`, `FORMULA_X`/`FORMULA_Y`,
  `HTTP_URL`, `URL_SCHEME`; `describe` is the wording error messages use.
- **`envelope`** — the plan object's enforced shapes: `actions` (array ≤ MAX_ACTIONS of
  objects), `variants` (array ≤ MAX_VARIANTS of `{label: string, actions}`).
- **`profiles`** — per profile: its surfaces and its op list **in prompt order** (the order
  a surface's entries are assembled in); `notes` explain within-profile membership drift,
  which is structured on the entries themselves (`surfaces`).
- **`forbidden`** — the §13 core list plus each surface's own list (`perSurface`), which
  each surface loads as its forbidden set. Enforcement policy stays per surface: cli and
  mcp hard-fail a plan naming one, the others skip it at parse and refuse at the executor
  (fixtures 207–217).
- **`ask`** — the §11 card: `schema` (the same key-spec language as ops, plus `forms` on
  the image reference and `exclusive` on an option), `defaultCustomLabel`, `limitsRef`.
- **`opsets`** — nested op sets: `extensionOpen` is what `open.actions` accepts (§8): a
  listed op validates with its `overrides` keys, else with the core entry's keys; an op in
  `failOps` fails the plan; anything else drops with a warning.
- **`ops`** — one entry per operation (`id` = `name`, except `filter.extension`).

## An op entry

- `name`, `id`, `profiles`, `baseline` (whose validator the schema was measured from).
- `surfaces` — optional: restricts membership within the profile to these surfaces
  (`openFile` → desktop + cli, `voiceChat` → browser, `accent`/`reconnect`/`copy` on the
  console side → cli). Absent = every surface of the profile.
- `keys` — the field schema (below). `surfaceKeys` — optional per-surface replacement of
  the whole `keys` map (the extension's three-value `theme.mode`, the cli console's
  `crop.spec.album`, the field-less cli `copy`).
- `forms` — exactly-one-of key groups: the present keys (among those the forms mention)
  must equal exactly one group, e.g. page `[["format"],["width","height"]]`.
- `together` — all-or-none groups (`blank`: `[["width","height"]]`).
- `exclusive` — at-most-one groups (an ask option's `actions` / `image`).
- `minFields` — at least this many declared keys present (`lineStyle`, `view`,
  `chatPanel`, the extension `filter`).
- `rules` — native rules the surface implements itself, run **before** the key checks on a
  copy of the action. Today: `cropAspectFold` (§3.2 — `aspect` beside `spec` folds in;
  a conflicting duplicate fails).
- `flags` (`editorSetting`, `topLevelOnly`, `newFrame`, `deferred`, `consoleSetting`,
  `gather`, `panelSettings`, `profileOp`), `surfaceFlags` — per-surface/profile additions
  merged over `flags`; `requires` — capability names the prompt assembly needs wired.
- `bullet` — the §13 prompt bullet verbatim (`null` + `bulletSharedWith` when it rides a
  sibling's bullet); `bulletVariants` — per-surface or per-profile replacement bullets
  (a string; `""` = this surface advertises no bullet), or `{addendum}` lines a console appends; `also`/`alsoOrder` — the browser's
  §10 "also accepts" lines.
- `divergence` — measured cross-surface differences that are **not** schema differences
  (execution honour splits, the desktop's real-CSS-name check, forbidden-op policy), each
  with its fixture.

## A key spec

`type` is one of `string | integer | number | boolean | object | array`; `integer` is a
finite number with no fractional part, `number` a finite number. Then, per type:

| property | applies to | meaning |
|---|---|---|
| `required` | any | must be present (`null` counts as absent everywhere; an unknown key fails the op) |
| `default` | any | the value the normalizer substitutes when absent |
| `enum` | string, integer, boolean | allowed values (`[true]` = must be exactly true) |
| `range` | integer, number | inclusive `[lo, hi]`, `null` = open end |
| `maxChars` | string | cap (number or limit name); **every** string is capped at `MAX_STRING_CHARS` when unset |
| `nonEmpty` | string | must be non-blank after trimming |
| `trim` | string | checks run on the trimmed value, and the normalizer stores it trimmed |
| `regex` | string | a grammar name or a list of alternatives — one must match |
| `regexBy` | string | `{key, map}`: the grammar depends on a sibling's value (formula `expr` by `axis`) |
| `regexNot` | string | must NOT match (save `path` vs `URL_SCHEME`) |
| `literals` | string | exact values accepted before the grammar (`""`, `"transparent"`) |
| `blankOk` | string | a blank value skips the grammar (formula `expr` clears the axis) |
| `onlyWith` | any | `{key: [values]}`: may be present only when the sibling has one of the values |
| `requiredWith` | any | `{key: [values]}`: must be present when the sibling has one of the values |
| `minItems`, `maxItems` | array | bounds (number or limit name) |
| `items` | array | the element spec |
| `fields` | object | the nested key map (unknown nested keys fail unless `allowUnknown` is set on the object spec); `minFields` / `forms` / `together` / `exclusive` apply to objects too |
| `allowUnknown` | object | tolerate undeclared keys (the envelope's variant objects — only ops are strict) |
| `opset` | array of actions | validate the elements as nested actions of this op set |
| `note`, `unit`, `rangeRef` | any | documentation only |

Check order (what a validator implements): native `rules` → unknown keys → `forms` /
`together` / `exclusive` / `minFields` → per key in declaration order: `required` /
`requiredWith` when absent, else `onlyWith` then the type check with its properties.
The reference implementation is `browser/js/llm/opSchema.js` (byte-identical in the
extension); the other surfaces port it rule-for-rule in their language.

## Rules

- **The registry drives; surfaces follow.** A behaviour change lands here first, then in
  the fixture corpus, and every surface's walker proves it landed everywhere. Never
  hand-code a check a key spec can express.
- **Divergences are structured, not prose.** Membership → `surfaces`; a different key
  schema → `surfaceKeys`; a different bullet → `bulletVariants`; a different flag →
  `surfaceFlags`. `divergence` prose is for non-schema differences only.
- **Native rules are the exception, not the escape hatch.** Add one only when the
  key-spec language genuinely cannot express the check, and implement it on every surface
  that registers the op.
