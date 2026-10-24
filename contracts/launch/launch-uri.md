# The launch URI

One URL grammar every front-end reads: a Stencil link opens a picture, a project or a blank page,
connects to collaboration servers, and sets what a typed console line could set. It rides the
browser app's own URL, so a chat, a bot, a terminal or another Stencil app can hand a session over
as a plain link.

```
https://<browser app>/?<pairs>          the query — logged by servers, kept in history
https://<browser app>/#<pairs>          the fragment — never leaves the machine
stencil://open?<pairs>                  the desktop scheme, the same pairs
```

`<pairs>` is `key=value&key=value`, `application/x-www-form-urlencoded` (`+` is a space). The query
and the fragment may both carry pairs; they merge, the query first. The two JSON hand-offs,
`#stencil=<json>` and `#stencil-desktop=<url>`, are a different carrier and are left alone.

The vocabulary is **`common/config/launchQuery.json`** (every spelling of every key, the secret
keys, the theme and chat words), and the conformance corpus is
**`common/fixtures/deepLink/launchQuery.json`**, walked by every surface's parser:
`browser/js/core/launch/query.js` (the reference), `cli/src/params/uri.zig`,
`desktop/src/app/open/launchUri.cpp` and `pystencil/pystencil/cli/launch/uri.py`. Each parser reads
the whole grammar; what a surface then *applies* is its own subset (§5).

## 1. Keys

A key is matched after folding: `main-color`, `main_color`, `mainColor` and `MAIN-COLOR` are one
key. The first value of a key wins; `connect` and `token` repeat. A key the grammar does not name is
an **editor setting** candidate (§3). `open=<id>` is the browser's own project deep link and is not
part of this grammar.

| Key (spellings) | Value | Meaning |
|---|---|---|
| `server`, `id`, `version` | origin · project id · number | A server project, the `stencil://` form; `version` is the one the link was made at |
| `project` | `[<origin>/projects/]<id or name>` | A project locator. With an origin it names the server; a bare ref is the launch's server's (`connect=` or `server=`), else a saved local project |
| `source` (`src`) | `http(s)` or `data:` URL | A picture or a video to open — its full URL, which may be a connected server's own path |
| `frame` | seconds | Into a video, when `source` is one; absent = `0` |
| `blank` | a colour | A blank page in that colour (`#ffffff` for an empty value) |
| `width`, `height` | px | The blank's size, both or neither |
| `layout` | `http(s)` URL, or inline JSON starting with `{` | A layout to apply once the picture is in |
| `layout-raw` | inline JSON object | The inline form, winning over `layout` |
| `incognito` | flag | Edit without saving; alone it opens an empty incognito editor |
| `script` | `.stc` text | A script handed over, at most `LAUNCH.scriptMaxChars`; every receiver opens it and runs nothing — a `script-mode` (`open` / `run`) beside it is read and ignored |
| `name` | text | The picture's name, and the project's |
| `description` (`desc`), `keywords`, `project-color` (`color`), `expire` (`expiration`, `expires`) | text | The saved project's fields; `expire` takes a duration (`2 weeks`, `off`) or a date |
| `theme` | `dark` · `light` · `auto` / `system` | The appearance mode |
| `main-color` (`accent`) | a preset key or `#rrggbb` | The accent |
| `connect` (`connection`) | a server URL, repeatable | Servers to connect, before anything opens |
| `token` | a credential, repeatable, **fragment only** | Pairs with the `connect` at the same position; a session or an admin token, probed as the server's connect dialog would |
| `llm-provider`, `llm-model`, `llm-base-url` (`llm-url`), `llm-server-url` (`llm-server`) | text | The assistant's settings (llm-contract §5) |
| `llm-api-key` (`llm-key`, `llm-token`) | **fragment only** | The assistant's key; on the anthropic wire a session key, never stored (llm-providers §5) |
| `chat`, `chat-dock` | `open` / flag · `left` `right` `top` `bottom` `float` | The chat panel |

A **flag** is on for an empty value (`?incognito`), `1`, `true`, `yes` or `on`.

## 2. Precedence and carriers

- The picture is one of `server`+`id` › `project` › `source` › `blank`, in that order.
- Connections are applied first, then the assistant settings, then the picture, then the layout,
  the name and the project fields, then the chat.
- **A credential rides the fragment only.** `token` and `llm-api-key` in the `?query` are dropped
  with a notice: the query lands in server logs, proxies and history; the fragment never leaves the
  machine. Everything else may ride either carrier.
- A server a link names that no saved connection knows is **asked about first**; a link never
  silently persists a connection to an unknown origin.
- A link is consumed once: the browser strips the pairs from its URL at once, so a reload repeats
  nothing.

## 3. Editor settings

Any other key is a `window.stencil` setting, camel-cased and coerced the way a console setter would
read it — `true`/`yes`/`on` and `false`/`no`/`off` to booleans, a number-shaped value to a number,
the rest as text: `?line-color=red&thickness=3&show-points=false&page-size=a4`. A key no setting
answers to is one notice. Only the browser applies them.

## 4. What a parser answers

Every parser produces the same shape, pinned by the corpus (the fields a vector omits are the empty
query's):

```jsonc
{ "picture": null | { "kind": "server", "url", "id", "version" }
                 | { "kind": "project", "server": string|null, "ref" }
                 | { "kind": "source", "url", "frame": number|null }
                 | { "kind": "blank", "color", "width": number|null, "height": number|null },
  "name": string|null, "layout": null | { "url" } | { "data": {…} },
  "incognito": bool, "script": string, "theme": "dark"|"light"|"system"|null,
  "settings": { camelKey: string|number|bool }, "llm": { provider?, model?, baseUrl?, serverUrl?, apiKey? },
  "chat": { "open": bool, "dock": string|null }, "connect": [ { "url", "token" } ],
  "project": { "description", "keywords", "color", "expire" },
  "warnings": [ { "code": "secret-in-query"|"bad-json"|"bad-value", "key" } ],
  "fromHash": bool, "opensSession": bool }
```

A URL is **actionable** — a launch link rather than a picture URL that happens to carry
grammar-looking parameters — when it names a picture, a connection, a layout, the theme, the
assistant or the chat; settings alone (`main-color` included) do not make one.
`https://cdn.example/a.png?width=800` is a picture.

## 5. Per-surface support

| Key | browser | browser console `/open` | cli one-shot (`-i`, positional) | cli console `/open` | desktop (argv, `stencil://`, console `/open`) | pystencil one-shot (`-i`) | pystencil console `/open` |
|---|---|---|---|---|---|---|---|
| `server`+`id`, `project` | ✓ (local ref by name or id) | ✓ | ✓ (`--server -i`) | ✓ (`/fetch`) | ✓ (bare ref = `--project`) | — | ✓ (`/fetch`) |
| `source`, `frame` | ✓ | ✓ | ✓ (`-i -f`) | ✓ | ✓ (`--src --frame`) | ✓ | ✓ |
| `blank`, `width`, `height` | ✓ | ✓ | ✓ (`--blank`) | ✓ (`/blank`) | — | ✓ | ✓ |
| `layout`, `layout-raw` | ✓ | ✓ | ✓ (`-l`) | ✓ (`/apply`) | ✓ | ✓ | ✓ |
| `incognito` | ✓ | ✓ | n/a (never saves) | n/a | ✓ | n/a | n/a |
| `script` | ✓ (Script window) | ✓ | — | said, left out | ✓ (Script window) | — | said, left out |
| `name`, project fields | ✓ | ✓ | `name` → `--remote-name` | server project's | — | — | said, left out |
| `theme`, `main-color`, settings | ✓ | ✓ | — | said, left out | `theme` | — | said, left out |
| `connect`, `token` | ✓ | ✓ | `token` → `--token` | ✓ (`/connect`) | ✓ | — | ✓ (`/connect`) |
| `llm-*` | ✓ | ✓ | — | ✓ (`/llm`) | — | — | ✓ (`/llm`) |
| `chat`, `chat-dock` | ✓ | ✓ | — | said, left out | — | — | said, left out |

"said, left out" is one notice naming what the surface has no verb for; "—" is silently ignored
(the one-shot modes print only their stderr contract).
