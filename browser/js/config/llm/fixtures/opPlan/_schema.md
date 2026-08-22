# Op-plan conformance fixtures

Shared, language-neutral test corpus for every surface's op-plan validator
(llm-contract.md §1–§2, §8, §10, §11.1). One JSON file per case, named
`NNN-kebab-description.json`:

- `name` — the file's slug (must match the filename).
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
  local override files — those files (`tests/fixtureOverrides.json` etc.) are reserved for
  surface-SPECIFIC output pins in the other fixture families, where the divergent value
  (own error wording, own sanitizer text, bodyPatch, …) has no cross-surface representation.

Walkers: run each fixture whose `profiles` include the surface's profile (or `all`) through
the surface's real parse/validate entry point and assert the verdict — see
`browser/tests/opPlanFixtures.test.js` (the reference walker). Extension fixtures assume a
scan listing of 8 images and a shared-tabs listing of 4 tabs.
