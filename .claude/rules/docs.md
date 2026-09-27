---
description: How a surface's ARCHITECTURE.md, README.md and USECASES.md are written — the seven fixed sections, and what each kind of doc never carries
paths:
  - "**/ARCHITECTURE.md"
  - "**/README.md"
  - "usecases/docs/**"
---

# Surface docs

A surface's `ARCHITECTURE.md` is an independent document of its design and stays current with
the tree, written as declarative sentences about the tree as it is: no gaps, status or planned
work, no fix, migration or refactor prose, no procedures, budgets or move proofs, and no
per-file listings (a file earns its own row only when a rule attaches to it). The `README.md`
beside it is user-facing only (build, run, use) and never carries architecture.
`usecases/docs/<app>/USECASES.md` is the illustrated companion: scenarios and steps only, its
images generated under `usecases/docs/<app>/img/` by `usecases/capture-runner/` (a visual change
is followed by a re-run, never by editing a picture), no architecture, no feature inventory, no
counts.

Every `ARCHITECTURE.md` carries the same seven `##` sections in this order, each in one fixed
form — a table where every item has the same fields, a list where they are parallel but
uneven, prose where it is one argument:

| Section | Form | Holds |
|---|---|---|
| Layers | prose | the import order left to right, and what lints it |
| Where things go | table | path · holds · rule |
| Entities | `classDiagram` + table | entity · what it is · owned by / lifetime · relates to |
| Patterns | table | pattern · where · notes |
| Design | bold-led bullets | the flows, and any schema this surface owns |
| Rules | numbered list | the invariants |
| Tests | prose | what the suite proves, what it stubs, what it pins |

A new entity, pattern or flow goes into its section, never a new heading. Name what a thing
*is*, never how many there are: counts of suites, files, pinned states or cases go stale on
the next commit and are not design.
