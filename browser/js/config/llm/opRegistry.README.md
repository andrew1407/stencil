# opRegistry.json — the normative op registry

Machine-readable record of the LLM op-plan surface (`llm-contract.md` §1–§13) across all
seven implementations, documenting **current measured reality** — not a reconciled ideal.
Where surfaces disagree today, the disagreement is *recorded* (a `divergence` entry, a
profile `note`), never invented away; reconciling schemas is a later step's job. The
merged fixture corpus (`fixtures/opPlan/`, its `profiles` + `knownDivergence`) is the
measured truth this file was cross-checked against.

Guarded by `browser/tests/opRegistryCanon.test.js`: the editor profile, limits, regexes,
browser bullets/flags/keys and the corpus cross-check are all pinned to the LIVE
`browser/js/llm/opPlan.js` structures, so this file cannot silently drift from the
browser reference.

## Schema

- **`$meta`** — sources (the seven surface registries + the corpus), the
  surface → profile map (browser/desktop → `editor`, cli/pystencil → `console`,
  `bot`, `mcp`, `extension`), and the guard test.
- **`limits`** — the shared §1 caps (`MAX_ACTIONS` 16, `MAX_VARIANTS` 8,
  `MAX_LAYOUT_LINES` 200, `MAX_FRAME_INDICES` 32, `MAX_STRING_CHARS` 5000), the name
  caps (`MAX_SAVE_NAME` 120, `MAX_RENAME_NAME` 80, `MAX_PATH_CHARS` 1024 for
  `save.path`), `MAX_UNDO_STEPS` 20, the §11
  `ask` caps (2/5 options, question 300, label 80, answer 500), and the
  extension-profile extras (`attachIndices`/`pinIndices` 8, panel-filter caps).
- **`regexes`** — token grammars as strings, sourced from the browser: `CROP_TOKEN`,
  `CROP_ASPECT` (positivity is checked beyond the regex), `PAGE_FORMAT`, `HEX`,
  `CSS_NAME`, `FORMULA_X`/`FORMULA_Y`. Op keys reference them via `regex`.
- **`profiles`** — per profile: its surfaces, TODAY's op-name list, and `notes`
  marking within-profile drift (ops one surface of the pair registers and the other
  skips as unknown). Measured sizes: editor 36 (browser 35 incl. browser-only
  `voiceChat`, + desktop-only `openFile`), console 23 (cli 23 ⊇ pystencil 19 —
  `accent`/`reconnect`/`copy`/`openFile` are cli-only), bot 24, mcp 10, extension 12.
- **`forbidden`** — the canonical §13 core list (browser's names) plus each surface's
  own list as shipped, and the enforcement split: cli and mcp hard-fail a plan naming
  a forbidden op, every other surface skips it at parse and rejects at the executor
  (corpus fixtures 207–217).
- **`ask`** — the §11 card's limits reference, default custom label, and the two
  measured ask-image divergences (fixtures 103/104).
- **`ops`** — one entry per operation (`id` = `name`, except `filter.extension`: the
  extension reuses the wire name "filter" for a *different* op, its panel-list
  filter). Each entry:
  - `name`, `profiles`, `baseline` — whose validator the `keys` schema records
    (browser wherever the browser registers the op; desktop/cli/bot/extension for
    the rest). **Where surfaces disagree on a key schema, `keys` stays the baseline
    and a `divergence` note names the surfaces** (e.g. `save.path` — accepted
    everywhere but honored only by desktop/cli/mcp; the extension's three-value
    `theme.mode`).
  - `keys` — field → `{type, required?, enum?, range?/maxChars?, regex?, default?,
    note?}`; nested object/array fields use `fields`/`items`.
  - `bullet` — the §13 prompt bullet **verbatim** (byte-exact against the baseline
    surface's registry; `null` + `bulletSharedWith` for redo/disconnect, which ride
    a sibling's bullet). `also`/`alsoOrder` are the browser's §10 "also accepts"
    lines. `bulletVariants` records, verbatim, each surface whose bullet text
    differs from the baseline (desktop/cli save, mcp's shortened layout, the
    console/bot connect and openUrl wordings, …).
  - `flags` — the baseline's flags (`editorSetting`, `topLevelOnly`, `newFrame`,
    `deferred`; `consoleSetting`/`panelSettings`/`gather`/`profileOp` for
    non-browser baselines), `requires` — the browser's capability names.
  - `divergence` — every measured cross-surface disagreement, with the corpus
    fixture that pins it.

## Rules

- Documenting, not driving (yet): implementations do **not** read this file today.
  Until table-driving lands, a change to any surface registry must be mirrored here —
  the canon test fails on browser-side drift, and the corpus cross-check fails when
  profile membership stops matching the fixtures.
- Never "fix" a divergence here: record it. Behavior changes happen in the surfaces
  first, land in the corpus, and only then get re-documented.
