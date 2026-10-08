# Stencil CLI contract

The canonical, single-source spec of the Stencil CLI's **argv (input) contract** and its
**stderr line grammar (output) contract** — the surface that non-`core/` adapters parse to
drive the CLI as a black box. Two adapters depend on it and must not silently drift from it:

- **`mcp/`** — the Rust MCP server. Builds argv in `mcp/src/args/` (`build_argv` in
  `argv.rs`, the flag literals as `FLAG_*` in `flags.rs`); parses stderr in
  `mcp/src/outcome.rs` (`PREFIX_*` + `parse_wrote` / `parse_all_wrote` / `parse_wrote_project` /
  `parse_documents` / `parse_notes` / `parse_remotes` / `extract_errors`, and `parse_scraped` in
  `mcp/src/outcome/scrape.rs`), and reads the stdout of §4.1 (`parse_diagnostics` in
  `mcp/src/outcome/diagnostics.rs`) and the documents of §4.3, §6 and §7.
- **`bot/`** — the .NET Telegram bot. Builds argv in
  `bot/src/Stencil.TelegramBot.Infrastructure/Cli/CliArgvBuilder.cs`; parses stderr in
  `.../Cli/CliOutcomeParser.cs`.

The `mcp/` literals are the **reference** for what the contract IS; this document and the
shared fixtures pin them to what the CLI (`cli/`) actually emits. When the CLI's flags or
output lines change, update **this file, the shared fixtures, and both adapters together**.

> This describes what the CLI does **today** (verified against `cli/src/params/parse.zig` and
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

Produced by `parse()` in **`cli/src/params/parse.zig`** (re-exported as `args.parse`). The grammar is small and
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
| `--thumbnail` | `<u32>` value, ≥ 1 | The pipeline's last step before encode: shrink the result so its longer side is at most this many pixels — area-averaged over premultiplied alpha, aspect kept, the shorter side rounded and at least 1. A result that already fits is written unchanged (never enlarged). The written file, the `wrote` line's `{W}x{H}` and any `--remote` / `--remote-update` upload carry the shrunk size; the page label keeps the full-size orientation. |
| `--server` | `<url>` value | Connect to a collaboration server; `-i` then names a **server project** to fetch/edit. |
| `--remote-update` | switch | With `--server`, write the result back into the fetched project. |
| `--remote` | `<url>` value | Upload the result as a **new** project on a server. |
| `--remote-name` | `<name>` value | Name for the `--remote` project (default: input image base name). |
| `--token` | `<tok>` value | Credential for `--server` and `--remote`: a session token or the server's admin token. Without it, each URL takes its token from the environment — see [Server credentials](#server-credentials). |
| `--list-projects` | switch | **Report mode.** With `--server`, print every project's metadata as a JSON array on stdout. See §6. |
| `--project-info` | `<id\|name>` value | **Report mode.** With `--server`, print one project's metadata as a JSON object on stdout. See §6. |
| `--project-update` | `<id>` value | **Project mode.** With `--server`, write the `--set-*` fields onto that project in one version-guarded `PUT /projects/{id}`, then print its metadata as `--project-info` does. `<id>` is the server's id (`p_…`), never a name. See §6.3. |
| `--set-name` / `--set-description` / `--set-keywords` / `--set-color` / `--set-blank-color` / `--set-expires` | `<value>` each | The fields `--project-update` changes, at least one: a non-blank name; a description; comma-separated keywords (trimmed, the empty ones dropped); a colour (`#rrggbb` or a colour name, stored as `#rrggbb`); a blank project's fill colour; an expiry in epoch ms (`0` = never). `''` clears any but the name. |
| `--if-version` | `<n>` value | With `--project-update`: guard the write with this version instead of the one the project has now, so it is refused once a peer has saved past it. |
| `--project-files` | `<id>` value | **Project mode.** With `--server`, print the project's metadata plus each file it stores, with the format its first bytes name. See §6.4. |
| `--project-file` | `<id> <kind>` two values | **Project mode.** With `--server`, download that stored file (`original`, `result`, `video`, `chat`, `variant1`…`variant8`) to `<output>`, byte for byte. See §6.5. |
| `--probe` | switch | **Report mode.** Print the `-i` input's format, size, alpha and byte size (a video adds its duration and frame count) as a JSON object on stdout. See §6. |
| `--source-site` | `<url>` value | **Scrape mode.** Fetch a page, extract + filter its media, download the matches into `<output>` (a **directory**). Mutually exclusive with `-i`, `--blank`, `--server` (→ `DuplicateSource`). See §3. |
| `--source-count` | `<u32>` value | Items per page/group (default **5**; `0` = **all**, `--group` ignored). |
| `--group` | `<u32>` value | 0-based page index; window = `filtered[G*N : G*N+N]` (default 0). |
| `--source-filter` | `<s>` value | Category tokens, `\|`-joined: `img` \| `video` \| `background` \| `poster` (absent / `all` = every category). |
| `--source-format` | `<s>` value | Format tokens, `\|`-joined, e.g. `png\|jpg\|webp\|gif\|svg\|mp4` (absent / `all` = every format; unknown-ext items bucket as `etc`). |
| `--source-name` | `<s>` value | Regex matched against each media URL (**POSIX ERE, case-insensitive**; a Windows CLI build has no `regex.h` and degrades to a case-insensitive substring test). Absent / empty = every URL. An invalid regex is a hard error (`error: invalid --source-name regex …`, exit 1), as is a pattern longer than **200 characters** (`error: --source-name pattern is too long …` — a bound on regexec backtracking). Dialect note: only the common subset (`. * + ? [] ^ $ \| ()`) is guaranteed identical across the CLI (POSIX), pystencil (Python `re`) and the extension (JS `RegExp`). |
| `--source-min-width` / `--source-max-width` | `<u32>` value | Inclusive pixel width bounds (`0` = unset; images measured from a header sniff). |
| `--source-min-height` / `--source-max-height` | `<u32>` value | Inclusive pixel height bounds (`0` = unset). |
| `--no-clobber` | switch | Refuse to overwrite an existing output, checked on the name the result really lands under — a missing or unknown extension filled in first (`out` from a JPEG input is `out.jpg`), a `.stencil` name as given. It runs before anything is fetched or uploaded: `error: --no-clobber: '{path}' already exists`, exit 1. Applies to the pipeline and `.stencil` one-shots, to every `@save` of a `--script` run (§4.2) and to the `--script-emit` output (§4.4). Adapters that are asked not to overwrite (mcp's `overwrite=false`) pass it; the CLI is the authority on the resolved name. |
| `--confine-output` | switch | Refuse an output path that leaves the working directory: an **absolute** path, a leading `~`, or a symbolic link on the path that resolves outside it (a dangling one included), on top of the `..` traversal refused in every mode (`error: --confine-output: refusing to write outside the working directory: …`, exit 1). Off by default. Adapters that forward an LLM-chosen `<output>` (mcp, bot) pass it; it also confines the scrape destination directory. |
| `--script` | `<path>` value | **Script mode.** Run a `.stc` script (`-` reads stdin). A script with no `@source` block edits the `-i` input. Mutually exclusive with `--script-check` and `--script-plan` (→ `DuplicateSource`). See §4. |
| `--script-check` | `<path>` value | Print the script's diagnostics **to stdout** and exit 1 if any is an error. See §4. |
| `--script-plan` | `<path>` value | Print the script lowered to an op-plan envelope, as JSON **on stdout** (`-` reads stdin). Runs nothing and writes no file; exits 1 when the script has an error. See §4.3. |
| `--script-emit` | `<path>` value | With `--script`, write that `.stc` out as a runnable script for another surface instead of running it. The **output's extension** picks the target — `.js`/`.stcjs` → the browser facade, `.py`/`.pystc` → pystencil — and no other suffix is accepted. Mutually exclusive with `--script-check` and `--script-plan`. See §4.4. |
| `--plan-check` | `<path>` value | **Report mode.** Print core's verdict on a model reply's op plan as JSON **on stdout** (`-` reads stdin); exits 1 when the plan is invalid, 2 when the reply cannot be read. See §7. |
| `--plan-surface` | `<surface>` value | The surface whose schema judges `--plan-check`, and whose verdict `--script-plan` adds to each chunk: `cli` (default), `mcp`, `bot`, `pystencil`, `desktop`, `browser`. |
| `--plan-capabilities` | `<a,b,…>` value | The capabilities wired for `--plan-check` / `--plan-surface`: an op requiring another is an unknown op. Absent = all. |
| `--prompt` | `<text>` value | One assistant turn over the `-i` / `--blank` input with the provider the `STENCIL_LLM_*` environment names (see [LLM provider](#llm-provider)): the reply is printed, the validated plan runs, and `<output>` is written with the usual `wrote` line. Exits 1, writing nothing, when the model gives no usable answer (no key, an HTTP error, a truncated, refused or off-shape reply, an invalid plan). POSIX only, as the console is: a Windows build refuses it (`error: --prompt is not available on Windows …`, exit 1). No adapter emits it. |
| `--console`, `--repl` | switch | Interactive console mode (out of scope for this contract). |
| `-h`, `--help` | switch | Show help. |
| `<output>` | positional | Result path (last positional wins) — or, in scrape mode, the **destination directory** (created if missing; default `.`). A missing/unknown extension is auto-filled from the input format. |

### Server credentials

`--server` and `--remote` each dial with the first of:

1. `--token`;
2. the URL's own invite fragment (`<url>#token=<tok>`);
3. the `STENCIL_SERVER_TOKENS` entry for the URL's origin;
4. `STENCIL_SERVER_TOKEN`;
5. none — a self-issued session.

`STENCIL_SERVER_TOKENS` is a comma list of `origin=token` pairs, `origin` being
`scheme://host[:port]`. Origins compare by scheme, host (case-insensitive) and effective port
(`https://h` is `https://h:443`); a bare host is https unless it is loopback, as the CLI
normalizes a server URL. Only the first `=` splits a pair, and a malformed entry is skipped.
So one run's `--server` and `--remote` may each get their own token.

A token is never printed, and both variables are scrubbed from every child process the CLI
spawns (ffmpeg, ffprobe, the clipboard helpers), as `STENCIL_LLM_*` is. An adapter hands its
tokens over this way rather than on argv, where a process listing shows them; mcp does.

### LLM provider

`--prompt` (and the console's initial `/llm` values) read `STENCIL_LLM_PROVIDER` (`ollama` —
the default —, `openai-compat`, `anthropic` or `stencil-server`), `STENCIL_LLM_BASE_URL`,
`STENCIL_LLM_MODEL`, `STENCIL_LLM_API_KEY`, `STENCIL_LLM_SERVER_URL` and
`STENCIL_LLM_SERVER_TOKEN` (llm-contract §5). `anthropic` calls the Anthropic Messages API
directly (§6.5) with `STENCIL_LLM_API_KEY` as the user's own key, held by the process only; with
no key nothing is sent and the run fails `error: no API key for this session`. The key is sent
over `https`, or over plain `http` to a loopback host only — anything else is refused before
a byte leaves (`error: refusing to send the API key to '{host}' over plain http — use https`).

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
- `--plan-check` is mutually exclusive with the script modes, the §6 modes and `--source-site`
  (`DuplicateSource`) and ignores every editing flag. `--plan-surface` and
  `--plan-capabilities` ride only with `--plan-check` or `--script-plan` (`error: --plan-surface
  and --plan-capabilities ride with --plan-check or --script-plan`), and `--plan-surface` names
  `cli`, `mcp`, `bot`, `pystencil`, `desktop` or `browser`.
- `--thumbnail` rides only with the one-shot raster pipeline: with a script, report, scrape,
  console or `.stencil` mode it is refused (`error: --thumbnail shrinks a one-shot image
  result; it does not ride with this mode`), as is `0`; either refusal prints its `error:`
  line and the usage, exit 2, before anything runs.
- `--prompt` needs an `<output>` (`error: --prompt writes its result to an output path — name
  one`), refuses `--server`, `--remote` and `--remote-update` (`error: --prompt edits a local
  input; it does not ride with --server or --remote`), and is mutually exclusive with the
  script, report, scrape, `--plan-check` and console modes (`DuplicateSource`); like
  `--thumbnail`'s, each refusal is exit 2 before anything runs. It shares the `.stencil`
  one-shot, so `--thumbnail` does not ride with it.
- `--probe`, `--list-projects`, `--project-info`, `--project-update`, `--project-files` and
  `--project-file` are mutually exclusive (`DuplicateSource`). Each takes precedence over the
  editing pipeline, whose flags it ignores. `--probe` needs `-i` and refuses `--server`
  (`error: --probe needs -i <path|url>`); the others need `--server` (`error: --list-projects
  needs --server <url>`).
- A project mode's `<id>` is letters, digits, `_` and `-` only, and `--project-file`'s kind one
  of the twelve; `--project-update` needs at least one `--set-*` field, a `--set-*` field or
  `--if-version` needs `--project-update`, and `--set-name` refuses a blank name. Each refusal
  prints its `error:` line and the usage, exit 2, before anything is sent.

### Adapter argv order

The CLI parses order-independently, so argv order is cosmetic. Both adapters emit the same
fixed layout for readability:
`[--server <url>] -i <input|project> | --blank [page] [w] [h] [color]`, then
`[-f <n>] [-c <spec>] [--album] [-r <n>] [-l <path>] [--filter <mode>]`, then
`[--remote-update] [--remote <url>] [--remote-name <name>]`, then `[--no-clobber]` (mcp only),
then the positional `<output>`. Neither puts a token on argv: mcp hands the CLI its tokens in
`STENCIL_SERVER_TOKENS`, and the bot passes none (its CLI runs self-issue a session).
mcp's opt-in `preview` is a separate run after a successful write:
`-i <written file> --thumbnail 512 <temp>.png`, confined to the same root.

---

## 2. Output (stderr) contract

The CLI writes **everything** — banner, usage, errors, and the success line — to **stderr**;
**stdout stays empty** (the real result is the written file). The exceptions are the modes
that report instead of writing: `--script-check`, `--script-plan` (§4), the §6 modes and
`--plan-check` (§7). Adapters run the child with
`NO_COLOR=1` so the text is free of ANSI escapes. Lines are matched by prefix; unrelated
lines (banner/usage) are ignored. Parsers split on any newline convention and `trim()` each
line before matching.

### 2.1 Success line — `wrote …`

Emitted by `writeOutputLabeled()` in `cli/src/pipeline/steps.zig`:

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
> `wrote {path} (project)`. `(project)` also has no `x`, so `parse_wrote` skips it and mcp
> parses it as a project result instead (`parse_wrote_project`, over `parse_documents`): a
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

Emitted by `deliverToServer()` in `cli/src/pipeline/oneshot.zig` **after** the `wrote` line, when
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
error: ffmpeg did not finish within 60s — stopped
error: --remote-update needs --server <url> -i <project>
```

**Extract algorithm (reference: `extract_errors`):** collect every trimmed line starting with
`error:` and join them with `\n`. If none are found, fall back to the whole trimmed stderr;
if stderr is empty, return the fixed message `the stencil CLI failed without a message`.

---

## 3. Scrape mode (`--source-site`) output contract

Emitted by `runImpl()` in **`cli/src/scrape/run.zig`**. Like §2, everything goes to **stderr**;
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

## 4. Script mode (`--script`, `--script-check`, `--script-plan`, `--script-emit`) output contract

The `.stc` language itself is normative in [`contracts/stc/stc-contract.md`](../contracts/stc/stc-contract.md);
this section fixes only what the CLI prints.

**`--script-check` and `--script-plan` write to stdout**, as do the §6 report modes. Every
other mode keeps stdout empty; `--script` (the run mode) and `--script-emit` do too, reporting
through the same `wrote …` / `error: …` lines as the pipeline.

### 4.1 `--script-check` — one line per diagnostic, on stdout

```
<file>:<line>:<col>: error|warning: <message> [<CODE>]
```

`<file>` is the path as given, or `<stdin>` for `-`. `<line>` and `<col>` are 1-based.
`<CODE>` is the stable diagnostic code from the contract's §8 catalogue, which is what an
editor keys off rather than the prose. Diagnostics come in source order. A script with no
diagnostics prints **nothing**.

Exit code: **1** if any diagnostic is an error, **0** otherwise (warnings do not fail).

Consumers: mcp's `stencil_script_check` (`outcome::parse_diagnostics` splits each line) and
the VS Code extension's diagnostics.

### 4.2 `--script` — the run

Diagnostics go to **stderr** through the usual severity prefixes (`error: `, `note: `), each
carrying the same `file:line:col` and `[CODE]`. A script with any error runs **nothing** and
exits 1.

Every `@save` emits the standard §2.1 `wrote …` line, so an adapter parses a script run
exactly as it parses a one-shot. A bare `@save` writes beside its source with a `-stencil`
suffix (`shots/a.png` → `shots/a-stencil.png`), which is what makes a whole-directory script
safe to run in place. A run that saved nothing prints `note: the script saved nothing — add a @save`.

A `@source` naming a directory or a glob expands to every media file directly inside it, in
sorted order, and the block runs once per file. `--confine-output` applies to every `@save`.

With `--no-clobber`, every `@source` is expanded and every `@save` destination named — by the
rule above, the extension filled from its input — before the first block runs; one that exists
stops the run with `error: --no-clobber: '{path}' already exists`, exit 1, and nothing is
written. A destination the run itself wrote earlier is its own and is written again; one only a
mid-run listing reveals is checked at its write.

### 4.3 `--script-plan` — one op-plan envelope, on stdout

Exactly one JSON object, followed by a newline, and nothing else on stdout. Nothing is
written. The only reads: a header-only size probe of each block's local inputs; for a URL
input whose block draws lines (a `@line`, `@rect` or `@layout`), the guarded fetch `--script`
opens it with, its size read by the same header sniffer; and the layout documents a `@layout`
names, read or fetched as `--script` loads them. Exit code **1** if any diagnostic is an
error, **0** otherwise.

```json
{
  "version": 1,
  "script": "shots.stc",
  "diagnostics": [
    {"severity": "error", "code": "E_UNKNOWN_DIRECTIVE", "line": 2, "col": 3, "len": 4,
     "message": "unknown directive '@crp' — did you mean '@crop'?"},
    {"severity": "error", "code": "E_LINE_NEEDS_POINTS", "line": 6, "col": 9, "len": 1,
     "message": "@line needs at least two points (from the @use stencil at 9:3)",
     "related": {"line": 9, "col": 3, "len": 4}}
  ],
  "blocks": [
    {
      "index": 0,
      "source": "shots/",
      "sourceKind": "project" | "file" | "url" | "dir" | "glob",
      "inputs": ["shots/a.png", "shots/b.png"],
      "frame": 0,
      "dims": {"width": 1600, "height": 1200},
      "plans": [{"reply": "", "actions": [{"op": "crop", "spec": {"x1": "10%"}}]}],
      "saves": [{"input": "shots/a.png", "path": "out/a-stencil.png"}],
      "ops": [{"kind": "crop", "line": 2, "edit": 1, "strs": [""], "toks": ["10%", "", "", ""],
               "nums": [0]}],
      "perInput": [
        {"input": "shots/a.png", "dims": {"width": 1600, "height": 1200},
         "plans": [{"reply": "", "actions": [{"op": "openFile", "path": "shots/a.png"}]}],
         "saves": [{"input": "shots/a.png", "path": "out/a-stencil.png"}]},
        {"input": "shots/b.png", "dims": {"width": 800, "height": 600},
         "plans": [{"reply": "", "actions": [{"op": "openFile", "path": "shots/b.png"}]}],
         "saves": [{"input": "shots/b.png", "path": "out/b-stencil.png"}]}
      ]
    }
  ]
}
```

- `version` is this envelope's version — **1** — bumped only when a consumer must change.
  `script` is the path as given, or `<stdin>` for `-`.
- `diagnostics` carries every diagnostic in source order, the §4.1 fields split into keys.
  **A script with any error emits `"blocks": []`** — nothing in it is safe to act on — and
  still exits 1 with its diagnostics. A diagnostic raised inside an expanded template adds a
  last member `related`: the `@use stencil` call it came from, as `line`, `col` and `len`
  ([stc-contract §6](../contracts/stc/stc-contract.md)); no other diagnostic has one.
- `sourceKind` is `project` for the implicit block (the `-i` input, so `source` is `""`);
  `inputs` is that block's expanded files, in the order `--script` would run them.
- `dims` is the first input's pixel size from a header probe — for a URL, from the fetch above
  when its block draws lines — or `null` when there is none: a video, a local file with no
  readable header, a URL block that draws nothing. Shape ops are **omitted** when it is
  `null`: their lengths have nothing to resolve against.
- `plans` is the block's actions as plan objects an adapter runs one after another, each with
  an empty `reply`; a plan ends at the op-plan's own `limits.MAX_ACTIONS` (**16**) and after
  every `undo`. Every action is a validated op in the
  `common/config/llm/opRegistry.json` vocabulary — `openFile`/`openUrl`, `frame`, `crop`,
  `filter`, `layout`, `save`, `undo` — so an adapter can feed a plan straight to its op-plan
  executor, and the plans leave what a `--script` run saves. A `layout` sets the drawn lines
  to exactly its own (llm-contract §2), so **every `layout` carries every line that should
  show at that point**:
  - A `crop`'s edges are cropSpec token strings an executor reads as `--script` does, never
    by its own page: `%` and `px` as written; `cm`, `mm` and `in` as the `px` they make at
    96 dpi (`-` from the far edge kept, the shortest decimal that reads back exactly); and a
    crop naming one axis only gets the other written out, `0px` to the length `--script`
    derives from the probed size (the image's own aspect, landscape with `album`, which then
    goes). With `dims` `null` that axis stays unnamed, as written.
  - `@line` / `@rect` wait, and land before the next other edit or `@save` as one `layout`:
    the lines already shown, then the waiting shapes, each line carrying its `@use line`
    style — `pointColor` too when a `point` colour is set. The lines shown before a `crop`
    are carried as `--script` keeps its marks: scaled by the new width over the old, or gone
    when the crop turns an album view portrait or back (`core::cropChange`), so an
    executor's own recalc of them agrees. The points are image pixels in the frame the plan
    started in: after a `crop` in the same plan they carry that crop's origin, which the
    executor subtracts ([llm-contract §1](../contracts/llm/llm-contract.md)). The executor
    then clamps them into the image, where `--script` draws a point beyond the edge as it lies.
  - A `@layout` lands the waiting shapes, then one `layout` of the lines shown with the
    document's on top, loaded as `--script` loads it — a local file read, a URL fetched
    through the same guard; `replace` keeps the document's lines alone.
  - An `@undo` of shapes still waiting lands nothing; past them it is `undo` actions (at most
    `MAX_UNDO_STEPS` steps each) counting the executor's own history entries — one per
    action, llm-contract §2 — and a `layout` it cuts into lands its surviving shapes again,
    over the lines shown before it.
  - A `@frame` drops the shapes still waiting, as `--script` decodes the frame afresh.
  - What a plan cannot carry faithfully is **refused**: an error diagnostic spanning the
    directive (`line`, `col`, `len` of its `@word`), placed among the others in source order,
    with `"blocks": []` and exit code 1. The codes: `E_PLAN_TOO_MANY_LINES` (more than
    `MAX_LAYOUT_LINES` lines would show at once), `E_PLAN_LAYOUT_UNREADABLE` (the document
    does not load: the file cannot be read, the fetch failed, or it is not layout JSON),
    `E_PLAN_SOURCE_UNREADABLE` (a URL input whose block draws lines does not fetch as an
    image; the first drawing directive is named), and `E_PLAN_LINES_UNKNOWN` (with `dims`
    `null` — a video — a `crop` moved lines a later `@layout` lands on).
  - `--plan-surface bot` plans for a url-only surface whose scripts come from chat users
    ([llm-contract §10](../contracts/llm/llm-contract.md)): a local `@layout` is refused
    unread (`E_PLAN_LAYOUT_LOCAL`), and every fetch refuses loopback too, as the bot's own
    image guard does.
- `saves` is the concrete destination of every `@save`, one entry per input × save op, named
  by the same rule `--script` uses (§4.2).
- `ops` is the block's lowered op stream ([stc-contract §13](../contracts/stc/stc-contract.md)),
  one object per op in order, with every length token **as written** — unresolved — so an
  adapter can resolve it against any input the block names, where `plans` resolved against
  the first. `kind` is the op kind in lowercase (`open`, `frame`, `crop`, `filter`, `line`,
  `rect`, `layout`, `save`, `undo`); `line` its 1-based source line; `edit` its 1-based index
  among the block's edits (0 for an op that is no edit); `strs`, `toks` and `nums` the §13
  columns for that kind.
- `perInput` is the block's last member: one entry per `inputs` element, in the same order,
  each carrying that `input`, its own `dims` probe, the `plans` resolved against it (opening
  it, so a percentage lands on its own pixels), and its own `saves`. The block-level `dims`,
  `plans` and `saves` stay as above, so its first entry repeats them; a consumer that applies
  a plan to every file of a directory or glob reads the entries instead. With
  `--plan-surface`, each entry's plans carry `check` as well.

- With `--plan-surface <s>` (and optionally `--plan-capabilities`), every plan object gains a
  last member `check`: core's verdict on that chunk under surface `s`, the `result` document of
  §7 — so an adapter maps the chunk from `check.actions` with no spawn of its own. Without the
  flag there is no `check` member.

Consumers: mcp's `stencil_script_plan` returns the envelope as it came, its `script` renamed
`<inline>` for inline text.

### 4.4 `--script-emit` — the script re-written for another surface

`--script <in.stc> --script-emit <out>` writes one file and runs nothing. The target comes
from `<out>`'s extension alone (no language flag exists):

| Extension | Target | Calls |
|---|---|---|
| `.js`, `.stcjs` | the browser app's `window.stencil` facade | the twin of `browser/js/console/scriptRunner.js` |
| `.py`, `.pystc` | pystencil's `Editor` | the twin of `pystencil/pystencil/editor/script.py` |

Any other suffix is refused with `error: cannot emit '<out>': name it .js, .stcjs, .py or
.pystc` and nothing is written.

Success prints one line, in the §2.1 shape a non-raster write already uses:

```
wrote shots.pystc (python)
wrote shots.stcjs (javascript)
```

Neither parenthetical carries an `x`, so `parse_wrote` (§2.1) ignores them exactly as it
ignores `(layout)` and `(project)`.

**Emission is literal.** Length tokens (`10%`, `-1in`), `@source` specs and `@save` targets
are emitted as the script wrote them and resolved by the generated file when it runs, so an
emitted script is as general as its `.stc` and needs no input image at emit time. Templates
are expanded and `@undo`/`@redo` arrive already reconciled, because both are resolved when
the core lowers the script (stc-contract §6, §7).

**A target that cannot honour a directive refuses it**, rather than emitting something that
cannot run. The rows are stc-contract §10's, for the surface the file will run on:

```
error: shots.stc:1:1: the browser can only open a URL — 'shots/' is a local path
error: shots.stc:3:5: @frame needs a video decoder — use the CLI
```

Exit code **1** for a refusal, an unknown suffix, or any error diagnostic in the script;
**0** when the file is written. `--confine-output` applies to `<out>`, and with `--no-clobber`
an existing `<out>` — taken as written, no extension filled in — is refused before the script
is read: `error: --no-clobber: '<out>' already exists`, exit 1.

Consumers: mcp's `stencil_script_emit` (`outcome::parse_documents` reads the written path and
its target off the `wrote` line).

## 5. Shared golden fixtures

`cli/testdata/outcome_fixtures.json` is the language-neutral golden set for §2. It has three
sections — `wrote`, `remotes`, `errors` — mapping 1:1 to the three parser functions. Each
case is `{ name, stderr, expected }`, where `expected` is `null` / an object for `wrote`, a
(possibly empty) list of `{action:"updated"|"created", …}` objects for `remotes`, and the
exact string for `errors`. Both adapter test suites load this same file over a relative path
and assert their parser reproduces `expected`, so the two ports are kept byte-identical.

## 6. Report and project modes (`--probe`, `--list-projects`, `--project-*`) output contract

Each prints **exactly one JSON document** on stdout — minified, one line, then a newline. Only
`--project-update` (on the server) and `--project-file` (its `<output>`) write anything.
Failure prints `error:` lines on stderr (§2.3), leaves stdout empty and exits **1**; success
exits **0**. Every string in a document is JSON-escaped, so no control byte reaches a terminal.

Consumer: mcp — `stencil_probe` reads §6.1, and `stencil_projects` §6.2, paging the array
itself (`limit`, and an `after` cursor that is the last id of the previous page), or §6.4 when
asked for a project's files; `stencil_project_update` reads §6.3 and `stencil_project_file` §6.5.

### 6.1 `--probe -i <path|url>`

```json
{"format":"png","width":16,"height":12,"alpha":false,"bytes":1234}
{"format":"mp4","width":640,"height":360,"alpha":null,"bytes":83514,"durationMs":5000,"frames":150}
```

- A still is read from its header, never decoded: the first 1 MiB of a local file, or a URL's
  body (fetched through the CLI's fetch guard). `format` is `png`, `jpg`, `gif`, `bmp` or `webp`
  from the header, `tga` from stb's header reader; `null` where neither names it.
- `alpha` is `true`/`false` where the header says (PNG colour type or a `tRNS` chunk, WebP's
  flags; a JPEG is always `false`), else `null` (GIF, BMP, TGA).
- `bytes` is the file's size, or the URL body's length; `null` for a video URL.
- A **video** (by extension, as `-i` decides) is read by ffprobe, and adds `durationMs` (the
  stream's duration, else the container's; `null` when neither says) and `frames` (the stored
  frame count, else duration × average frame rate, rounded; `null` when unknown). `alpha` is
  `null`, `format` the file's extension. Without ffprobe, ffmpeg's first frame still gives
  the size and both keys are `null`; without either, `error: ffmpeg not found on PATH — needed
  only for video input`. A still never carries the two keys.
- A video **URL** is fetched through the same guard and 64 MiB cap as a still (redirects
  refused) before ffprobe or ffmpeg reads it, so a refused host prints the same
  `error: refusing to fetch …` line. ffmpeg and ffprobe are killed after 60 s with
  `error: <tool> did not finish within 60s — stopped`; a frame grab under `-i` is the same.

### 6.2 `--list-projects` / `--project-info <id|name>` (with `--server <url>`)

The CLI connects as every server mode does (§1 [Server credentials](#server-credentials)),
then walks `GET /projects?limit=500`, following `nextCursor` (as `&after=`) until a page has
none. A cursor handed back twice, or more than 1000 pages, is an error rather than a loop.

`--list-projects` prints every project, in the server's order, as an array; `--project-info`
prints the first whose `id` equals the value or whose `name` matches it case-insensitively,
as one object, and stops paging there — else `error: no server project with the id or name
"{value}"`, exit 1.

```json
[{"id":"p_1_a","name":"Plans","createdAt":1700000000000,"updatedAt":1700000500000,"hasImage":true,"imageW":800,"imageH":600,"version":3}]
```

Each project keeps only these fields, in this order, and only those the server sent:
`id`, `name`, `createdAt`, `updatedAt`, `expiresAt`, `hasImage`, `imageW`, `imageH`,
`version`, `source`, `resource`, `color`, `description`, `keywords`, `blankColor`, `blank`,
`originalHash`. The stored image, the layout, and the server's own paths and session fields
never appear.

### 6.3 `--project-update <id>` (with `--server <url>` and a `--set-*` field)

The CLI connects as in §6.2, reads the project's current `version` (`GET /projects/{id}`) unless
`--if-version` names one, and sends one `PUT /projects/{id}` carrying only the fields given plus
that version. It prints the record the server answers with, cut to §6.2's fields:

```json
{"id":"p_1_a","name":"Plans v2","createdAt":1700000000000,"updatedAt":1700000900000,"hasImage":true,"imageW":800,"imageH":600,"version":4,"keywords":["floor","draft"]}
```

A stale version (a peer saved first) is the server's 409: `error: {server} rejected the update of
{id} (409): {message}`; an unknown id its 404, in the same form.

### 6.4 `--project-files <id>` (with `--server <url>`)

The project's metadata as §6.2 cuts it, then `files`: one `{kind, format}` per kind the project
stores, in the order `original`, `result`, `video`, `chat`, `variant1`…`variant8`. Each kind is
probed with a `GET /projects/{id}/files/{kind}` asking `Range: bytes=0-63`; a 404 means the
project holds none. `format` is what those bytes name — `png`, `jpg`, `gif`, `webp`, `bmp`,
`mp4`, `webm`, `json` — else `null`.

```json
{"id":"p_1_a","name":"Plans","version":3,"files":[{"kind":"original","format":"png"},{"kind":"variant2","format":"jpg"}]}
```

### 6.5 `--project-file <id> <kind> <output>` (with `--server <url>`)

Downloads `GET /projects/{id}/files/{kind}` and writes it to `<output>` exactly as served — no
extension is filled in and nothing is decoded. `<output>` is required and keeps every output
rule, all judged before anything is fetched: `..` is refused always, `--confine-output` refuses
an absolute or `~` path or one a link leads out, `--no-clobber` an existing file (§1). It then prints:

```json
{"id":"p_1_a","kind":"result","path":"out.png","bytes":48213,"format":"png"}
```

A kind the project lacks is the server's 404: `error: {server} refused the result file of p_1_a
(404): {message}`.

---

## 7. Op-plan check (`--plan-check <file|->`) output contract

`--plan-check` hands a model reply (the file, or stdin for `-`) to the core op-plan validator
(`core/opplan`, llm-contract.md §1–§2, §11) under one surface's schema, and prints its verdict:
**exactly one JSON document** on stdout — minified, one line, then a newline — writing no file.

| Flag | Value | Meaning |
|---|---|---|
| `--plan-check` | `<file>` or `-` | The reply text, verbatim — prose, fences and all. At most 8 MiB. |
| `--plan-surface` | `cli` (default), `mcp`, `bot`, `pystencil`, `desktop`, `browser` | Whose profile, surfaceKeys, surfaceRules and forbidden policy judge it. |
| `--plan-capabilities` | `a,b,…` | Only these capabilities are wired: an op whose `requires` names another is an unknown op. Absent = all. |

```json
{"version":1,"surface":"mcp","registryBytes":80359,"registryFnv1a64":"4526a9d034ee5b00","result":{"status":"valid","reply":"ok","actions":[{"op":"rotate","dir":"left","times":1}],"variants":[],"ask":null,"warnings":[],"error":null}}
```

- `version` is this envelope's version — **1** — bumped only when a consumer must change.
- `registryBytes` / `registryFnv1a64` are the size and the FNV-1a 64 (16 lowercase hex digits)
  of the `opRegistry.json` the CLI embeds. An adapter that also embeds the registry compares
  them once at startup: a mismatch means the CLI and the adapter were built from different
  registries.
- `result` is core's document, byte-equal to what `browser/js/llm/plan/parser.js` `walkPlan`
  returns (pinned by `common/fixtures/llm/opPlan/generated/normalized.json`):
  - `status` — `valid`, `chatOnly` (no JSON object: `reply` is the trimmed text) or `invalid`.
  - `actions` — each normalized: `{op, …declared keys}`, defaults applied, trims honoured.
  - `variants` — `{label, actions}`; `label` is the model's own, or `null`.
  - `ask` — `null`, or `{question, mode, allowCustom, customLabel, options:[{label, actions?,
    image?}]}`.
  - `warnings` — `{code, …, message}`: `W_UNKNOWN_OP` (`op`), `W_VARIANT_DROPPED` and
    `W_PREVIEW_DROPPED` (`op`, 1-based `index`, `label`), `W_REPLY_OMITTED`.
  - `error` — `null`, or `{code, …, detail, message}`: `E_ACTION` (`op`), `E_FORBIDDEN`
    (`op`), `E_JSON_LIMIT` (`limit`, `max`), `E_PLAN` (`rule` — `notArray`, `tooMany`,
    `notAction`, `variantsNotArray`, `tooManyVariants`, `variantNotObject`, `variantLabel`,
    `ask` — with `scope`, `index`, `item`, `isObject`, `isString`, `max` where they apply).
    `message` is the canonical wording — mcp shows it as written; an adapter may still reword
    from the code and its fields.

Exit code **0** for `valid` and `chatOnly`, **1** for `invalid` (the document is still
printed), **2** when there is nothing to judge: an unreadable reply (`error: cannot read the
reply {label} ({reason})`, `error: that reply is too large: …`) or a usage error in argv (banner
and usage on stderr). Stdout is empty on exit 2.

Consumers: mcp and bot validate a model's plan through this mode instead of a validator of
their own — `stencil --plan-check - --plan-surface mcp` (or `bot`), the reply on stdin — and
map `result.actions` onto their executors.

