# Op-plan conformance fixtures

Shared, language-neutral test corpus for every surface's op-plan validator
(llm-contract.md §1–§2, §8, §10, §11.1). The hand-written cases live in ONE bundle,
`cases.json` (`{cases: [...]}`), each entry carrying:

- `file` — the case's stable label, `NNN-kebab-description.json`; walkers report under it,
  and the `NNN` ordinals are historical (a gap is a retired case).
- `name` — that label's slug (the `NNN-` prefix stripped). This is the key every surface's
  override file and `knownDivergence` use.
- `profiles` — which surface profiles the case applies to: `"editor"` (browser + desktop),
  `"console"` (cli + pystencil), `"bot"`, `"mcp"`, `"extension"`, or `"all"`.
  Invalid-param cases MUST be profile-restricted to surfaces that register the op —
  everywhere else the op is an unknown-op skip and the plan is valid (§1).
- `input` — the raw model output. An OBJECT is serialized and fed to the parser; a STRING
  is fed verbatim (for fence-stripping / JSON-extraction / duplicate-key cases).
- `expect` — `"valid"` (parse/validate succeeds; chat-only counts as valid) or
  `"invalid"` (the plan is rejected). Verdict level only — normalized output is
  deliberately NOT encoded here (per-surface normalizers differ).
- `reason` — required for invalid cases; also carries notes on suspected
  spec-vs-implementation mismatches. The corpus pins what implementations DO.
- `knownDivergence` — optional `{ "<surface>": "valid"|"invalid" }` overrides for measured
  cross-surface disagreements, keyed by surface (`browser`, `desktop`, `cli`, `pystencil`,
  `bot`, `mcp`, `extension`). A surface's walker uses its override when present, else `expect`.
  This is the CORPUS-AUTHORITATIVE record of every verdict-level disagreement: all measured
  opPlan verdict divergences live here (with the evidence in `reason`), not in the surfaces'
  local override files — those files (`tests/llm/op/fixtureOverrides.json` etc.) are reserved for
  surface-SPECIFIC output pins in the other fixture families, where the divergent value
  (own error wording, own sanitizer text, bodyPatch, …) has no cross-surface representation.

Since the validators became table-driven from `opRegistry.json` (schemaVersion 2), a
verdict divergence can only come from a recorded surface difference (`surfaces`,
`surfaceKeys`, a native post-check, or forbidden-op policy) — never from a hand-coded
check drifting. Fixtures 103/104/160/199 lost their overrides in that pass.

**Generated cases.** `generated/cases.json` (one bundle, `{cases: [...]}` in the same shape,
each case named `gen-<op>-<rule>`, with no `file`) is DERIVED from `opRegistry.json` by
`browser/tools/genOpPlanFixtures.mjs` (`npm run gen-fixtures`): the minimal valid action per
op, unknown/missing/wrong-type fields, enum and grammar rejections, range and cap boundary
pairs, the cross-field presence rules, the plan envelope caps and the §11 card. Every walker
loads it beside `cases.json` (each generated case walks as `<name>.json`);
`browser/tests/opPlanFixtures.test.js` compares the sha256 of `opRegistry.json` and of
`generated/cases.json` against `generated/freshness.json` instead of re-deriving the bundle,
so a registry edit without `npm run gen-fixtures` fails it. Hand-written cases
are reserved for what the registry cannot derive: extraction tolerance, the aspect fold,
variant/preview drops, forbidden-op policy, and every recorded divergence.

Adding a case: append it to `cases.json` under the next free `NNN` label.

Walkers: run each fixture whose `profiles` include the surface's profile (or `all`) through
the surface's real parse/validate entry point and assert the verdict — see
`browser/tests/opPlanFixtures.test.js` (the reference walker). Extension fixtures assume a
scan listing of 8 images and a shared-tabs listing of 4 tabs.
