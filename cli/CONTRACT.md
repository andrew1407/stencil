# Stencil CLI contract

The canonical, single-source spec of the Stencil CLI's **argv (input) contract** and its
**stderr line grammar (output) contract** — the surface that non-`core/` adapters parse to
drive the CLI as a black box. Two adapters depend on it and must not silently drift from it:

- **`mcp/`** — the Rust MCP server. Builds argv in `mcp/src/args.rs` (`build_argv`, flag
  literals as `FLAG_*`); parses stderr in `mcp/src/outcome.rs` (`PREFIX_*` +
  `parse_wrote` / `parse_remotes` / `extract_errors`).
- **`bot/`** — the .NET Telegram bot. Builds argv in
  `bot/src/Stencil.TelegramBot.Infrastructure/Cli/CliArgvBuilder.cs`; parses stderr in
  `.../Cli/CliOutcomeParser.cs`.

The `mcp/` literals are the **reference** for what the contract IS; this document and the
shared fixtures pin them to what the CLI (`cli/`) actually emits. When the CLI's flags or
output lines change, update **this file, the shared fixtures, and both adapters together**.

> This describes what the CLI does **today** (verified against `cli/src/args.zig` and
> `cli/src/pipeline.zig`), not what it should do. The console/REPL mode (`--console`) is a
> separate interactive surface and is **not** part of this adapter contract.

## Where this is enforced

- **Argv grammar** ⇒ each adapter's argv builder (`build_argv` / `BuildArgv`), unit-tested in
  `mcp/tests/args_test.rs` and `bot/tests/.../CliArgvBuilderTests.cs`.
- **Output grammar** ⇒ each adapter's stderr parser, unit-tested in
  `mcp/tests/outcome_test.rs` and `bot/tests/.../CliOutcomeParserTests.cs`, **and** pinned to
  a shared, language-neutral golden set: **`cli/testdata/outcome_fixtures.json`**, replayed
  by `mcp/tests/fixtures_test.rs` and `bot/tests/.../SharedOutcomeFixturesTests.cs`. Both
  suites load the *same* file, so a divergence between the two parsers is caught by CI.

---

## 1. Argv (input) contract

Produced by `parse()` in **`cli/src/args.zig`**. The grammar is small and
**order-independent** (flags may appear in any order); a bare token that isn't a flag or a
flag's value is the positional **output** path (last one wins). There is **no `--`
end-of-options terminator** — a bare `--` is rejected as an unknown flag — so an output path
that begins with `-` would misparse as a flag; adapters reject dash-leading outputs up front.

### Flags

| Flag (aliases) | Arg shape | Meaning |
|---|---|---|
| `-i`, `--input` | `<path\|url>` value | Image/video source: local path or `http(s)://` URL. With `--server`, this is instead the **name of a project** to fetch. Mutually exclusive with `--blank` (→ `DuplicateSource`). |
| `--blank` | `[format] [w h] [color]` (optional trailing tokens) | Create a blank page. Optional leading ISO page-format name (`a0`…`a10`, `b0`…`b10`, `c0`…`c10`, case-insensitive), **or** an explicit integer `w h` pair (mutually exclusive with a format; giving both errors), then an optional color name / `#hex`. Omit all ⇒ A4 @ 96 dpi, white. Mutually exclusive with `-i`. |
| `-f`, `--frame` | `<u32>` value | Video frame index to grab (default 0). |
| `-c`, `--crop` | `"<spec>"` value | Crop spec, e.g. `"x1=10% x2=90% y1=10% y2=90%"`. |
| `--album` | switch | On a single-axis crop, derive the missing axis from the page proportion (landscape). |
| `-r`, `--rotate` | `<i32>` value | Rotate `int × 90°` (negative = counter-clockwise). |
| `-l`, `--layout` | `<path\|url>` value | Layout JSON to draw onto the image. |
| `--filter` | `<mode>` value | `bw` \| `sepia` \| `invert` \| `contour` \| a color name/`#hex` (duotone tint). Overrides a layout-baked filter. |
| `--server` | `<url>` value | Connect to a collaboration server; `-i` then names a **server project** to fetch/edit. |
| `--remote-update` | switch | With `--server`, write the result back into the fetched project. |
| `--remote` | `<url>` value | Upload the result as a **new** project on a server. |
| `--remote-name` | `<name>` value | Name for the `--remote` project (default: input image base name). |
| `--source-site` | `<url>` value | **Scrape mode.** Fetch a page, extract + filter its media, download the matches into `<output>` (a **directory**). Mutually exclusive with `-i`, `--blank`, `--server` (→ `DuplicateSource`). See §4. |
| `--source-count` | `<u32>` value | Items per page/group (default **5**; `0` = **all**, `--group` ignored). |
| `--group` | `<u32>` value | 0-based page index; window = `filtered[G*N : G*N+N]` (default 0). |
| `--source-filter` | `<s>` value | Category tokens, `\|`-joined: `img` \| `video` \| `background` \| `poster` (absent / `all` = every category). |
| `--source-format` | `<s>` value | Format tokens, `\|`-joined, e.g. `png\|jpg\|webp\|gif\|svg\|mp4` (absent / `all` = every format; unknown-ext items bucket as `etc`). |
| `--source-name` | `<s>` value | Regex matched against each media URL (**POSIX ERE, case-insensitive**; a Windows CLI build has no `regex.h` and degrades to a case-insensitive substring test). Absent / empty = every URL. An invalid regex is a hard error (`error: invalid --source-name regex …`, exit 1). Dialect note: only the common subset (`. * + ? [] ^ $ \| ()`) is guaranteed identical across the CLI (POSIX), pystencil (Python `re`) and the extension (JS `RegExp`). |
| `--source-min-width` / `--source-max-width` | `<u32>` value | Inclusive pixel width bounds (`0` = unset; images measured from a header sniff). |
| `--source-min-height` / `--source-max-height` | `<u32>` value | Inclusive pixel height bounds (`0` = unset). |
| `--console`, `--repl` | switch | Interactive console mode (out of scope for this contract). |
| `-h`, `--help` | switch | Show help. |
| `<output>` | positional | Result path (last positional wins) — or, in scrape mode, the **destination directory** (created if missing; default `.`). A missing/unknown extension is auto-filled from the input format. |

### Mutual-exclusion / dependency rules (mirrored by both adapters)

The CLI enforces some of these in `args.zig`; the rest surface at pipeline time
(`cli/src/pipeline.zig`). Because a few would otherwise be swallowed silently (an unknown
`--blank` page-format or an unparseable color token is simply *not consumed* and the blank
falls back to A4/white with no error), the adapters validate them **before** building argv:

- `-i` / `--input` and `--blank` are mutually exclusive (`DuplicateSource` in `args.zig`).
- `--blank` page-format **and** explicit `w h` are mutually exclusive (`args.zig` prints
  `error: --blank takes a page format OR explicit dims, not both`). A lone `w` without `h`
  is invalid.
- `--server` requires `-i` (the project name) and is incompatible with `--blank`
  (`pipeline.zig`: `error: --server needs -i <server project name>`).
- `--remote-update` requires `--server` + `-i` (`pipeline.zig`:
  `error: --remote-update needs --server <url> -i <project>`).
- `--remote-name` is only meaningful with `--remote`.
- `--source-site` is mutually exclusive with `-i` / `--input`, `--blank`, and `--server`
  (`DuplicateSource` in `args.zig`); in scrape mode the editing flags (`-c`, `-r`, `-l`,
  `--filter`, `-f`, `--album`) and connection flags are ignored.

### Adapter argv order

The CLI parses order-independently, so argv order is cosmetic. Both adapters emit the same
fixed layout for readability:
`[--server <url>] -i <input|project> | --blank [page] [w] [h] [color]`, then
`[-f <n>] [-c <spec>] [--album] [-r <n>] [-l <path>] [--filter <mode>]`, then
`[--remote-update] [--remote <url>] [--remote-name <name>]`, then the positional `<output>`.

---

## 2. Output (stderr) contract

The CLI writes **everything** — banner, usage, errors, and the success line — to **stderr**;
**stdout stays empty** (the real result is the written file). Adapters run the child with
`NO_COLOR=1` so the text is free of ANSI escapes. Lines are matched by prefix; unrelated
lines (banner/usage) are ignored. Parsers split on any newline convention and `trim()` each
line before matching.

### 2.1 Success line — `wrote …`

Emitted by `writeOutputLabeled()` in `cli/src/pipeline.zig`:

```zig
logo.print("wrote {s} ({d}x{d} px · {s})\n", .{ resolved.path, img.width, img.height, page_label });
```

**Grammar:** `wrote {path} ({W}x{H} px · {page_label})`, where:

- `{path}` — the resolved output path (extension auto-filled). **May itself contain `" ("`**,
  so parsers locate the size tail with a **reverse** search for the last `" ("`.
- `{W}x{H}` — pixel dimensions; the `x` is ASCII `x` (U+0078). The parsed value is **only**
  this leading whitespace-delimited token of the parenthesised tail.
- `{page_label}` — informational, e.g. `A4 21×29.7cm` (from `pageLabelAlloc`; the cm size
  uses `×` U+00D7, never ASCII `x`). Parsers ignore everything after the leading `{W}x{H}`.

Older / alternate builds may print a bare `({W}x{H})` with no ` px · …` suffix; parsers
accept both (they read only the leading `{W}x{H}` token). Examples:

```
wrote /tmp/out.png (800x600)
wrote /tmp/out.png (1280x720 px · A4 29.7×21cm)
wrote /tmp/my (final) shot.png (1920x1080)
```

**Parse algorithm (reference: `mcp/src/outcome.rs::parse_wrote`, which `bot` mirrors exactly):**
strip the `wrote ` prefix; `rfind(" (")` to split `{path}` from the tail; require the tail to
end with `)`; take the **first whitespace-delimited token** of the inner tail; `split_once('x')`;
parse both sides as unsigned integers. Any step failing ⇒ this line is not a success line.

> Note: console mode also prints `wrote {path} (layout)` for a layout export; it is not a
> size line (`(layout)` has no `x`) and is out of scope for this contract. Likewise, saving a
> **`.stencil` project** — the console's `/save x.stencil`, or the one-shot when the input OR
> output is a `.stencil` (`-i in.stencil out.stencil`, `-i photo.png out.stencil`) — prints
> `wrote {path} (project)`. `(project)` also has no `x`, so `parse_wrote` ignores it too; a
> `.stencil` bundle is not a raster and carries no size token. (A one-shot `.stencil` **input**
> rendered to a raster output still prints the normal `wrote … ({W}x{H} px · …)` line.)
>
> The `.stencil` bundle carries optional string metadata (`color`, `description`, `source`,
> `resource`) alongside the image + layout; each is **omitted from the JSON when empty** and
> round-trips through `project.build`/`project.parse`. `description` is a free-text caption with
> no length limit in the core. In `--console` mode (out of scope for this contract) the
> `/project-description [<text…>]` command sets the active server project's description, or
> clears it when no text is given; the current value is shown in the `/projects` listing.

### 2.2 Server-delivery lines — `updated …` / `created …`

Emitted by `deliverToServer()` in `cli/src/pipeline.zig` **after** the `wrote` line, when
`--remote-update` and/or `--remote` are given. A single run can emit both.

**Update (`--remote-update`),** from
`logo.print("updated server result for project {s} ({d}x{d})\n", .{ fetched_id, w, h })`:

```
updated server result for project p_x_y (800x600)
```

Grammar: `updated server result for project {id} ({W}x{H})`. Parse: strip the
`updated server result for project ` prefix; `rfind(" (")`; strip trailing `)`;
`split_once('x')`; parse `{W}`/`{H}`.

**Create (`--remote`),** from
`logo.print("created server project \"{s}\" ({s})\n", .{ name, id })`:

```
created server project "My Shot" (p_a_b)
```

Grammar: `created server project "{name}" ({id})`. Parse: strip the
`created server project ` prefix; `rfind(" (")`; strip trailing `)` to get `{id}`; the
remaining head, trimmed of quotes, is `{name}`.

Reference: `mcp/src/outcome.rs::parse_remotes` (returns all deliveries found, in order).

### 2.3 Error lines — `error: …`

Failures print one or more lines beginning with `error:` (e.g. from `args.zig`, `net.zig`,
`pipeline.zig` — see `grep -rn "error:" cli/src`). Examples:

```
error: could not parse crop spec "oops"
error: unknown flag '--nope'
error: refusing to fetch internal/blocked host 'x'
error: --remote-update needs --server <url> -i <project>
```

**Extract algorithm (reference: `extract_errors`):** collect every trimmed line starting with
`error:` and join them with `\n`. If none are found, fall back to the whole trimmed stderr;
if stderr is empty, return the fixed message `the stencil CLI failed without a message`.

---

## 3. Scrape mode (`--source-site`) output contract

Emitted by `run()` in **`cli/src/scrape.zig`**. Like §2, everything goes to **stderr**;
**stdout stays empty**. A run downloads zero or more files into the output directory and
prints one line per file plus a final summary. Per-item fetch failures are **non-fatal**
(the run continues); **zero files written** is a hard error (exit 1).

### 3.0 Start line — `scraping …`

Before any network I/O the CLI emits a single progress line so the surface isn't silent
during the page fetch + downloads:

```
scraping https://example.com/gallery…
```

`scraping {url}…` echoes the requested `--source-site` URL (not the lowercased host used by
the per-file lines). It carries **none** of the parsed prefixes below (`wrote `, `scraped `,
`error:`), so the `mcp`/`bot` parsers ignore it — it is informational only. pystencil's
`_run_scrape` mirrors it.

### 3.1 Per-file line — `wrote …`

- Image (dimensions sniffed): `wrote {path} ({W}x{H} px · source {host})`
- Video / unmeasured item:     `wrote {path} (source {host})`

`{host}` is the **scraped page's** host (`net.hostOf(source-site)`), used identically on
every line and the summary. `{path}` is `<output-dir>/<filename>` — a sanitized last path
segment of the media URL, extension ensured from the format/content sniff, or
`source-{index}.{ext}` on a missing name or collision.

### 3.2 Summary line — `scraped …`

`scraped {n} file(s) from {host} into {dir}` — `{n}` files written, `{host}` the page host,
`{dir}` the output directory.

### 3.3 Error lines — `error: …`

- Per-item (non-fatal, run continues): `error: could not fetch {url} ({reason})`
- Zero matches (fatal, exit 1):        `error: no media matched at {url}`

### 3.4 Multi-file parse rule (for `mcp/` + `bot/`)

Every line starting with `wrote ` → `{path}` is the text between `wrote ` and the **last**
`" ("` (or the rest of the line if there is no `" ("`); dimensions are the leading `WxH` of
the parenthetical tail when it matches `^\d+x\d+`, else null (video lines have null dims).
`scraped {n} file(s) from {host} into {dir}` is the optional summary; `error: …` lines are
errors as in §2.3.

The scrape line shapes are pinned by the shared golden set
**`cli/testdata/scrape_fixtures.json`** (the CLI reproduces them; `mcp/` + `bot/` parse them).

---

## 4. Shared golden fixtures

`cli/testdata/outcome_fixtures.json` is the language-neutral golden set for §2. It has three
sections — `wrote`, `remotes`, `errors` — mapping 1:1 to the three parser functions. Each
case is `{ name, stderr, expected }`, where `expected` is `null` / an object for `wrote`, a
(possibly empty) list of `{action:"updated"|"created", …}` objects for `remotes`, and the
exact string for `errors`. Both adapter test suites load this same file over a relative path
and assert their parser reproduces `expected`, so the two ports are kept byte-identical.
