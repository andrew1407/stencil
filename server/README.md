# Stencil collaboration server (`server/`)

A Go server that stores and shares Stencil projects and runs **live, multi-client edit
sessions**. Any client holding a valid token sees the same project list and can open any
project; several browser / desktop / CLI / extension clients can edit one project at the
same time, with edits relayed live and durable snapshots committed under a version guard.
Project metadata lives in **Postgres**, image bytes in a path-confined file store, and live
edits fan out over **WebSocket and raw TCP** (optionally across instances via **Redis**).
For how it is built, see [`ARCHITECTURE.md`](ARCHITECTURE.md).

## Run

```bash
cp .env.example .env          # set DATABASE_URL at minimum
go run ./cmd/stencil-server   # REST/WS on :8090, TCP edit channel on :8091
```

Requires a reachable Postgres (`DATABASE_URL`); the schema is created at boot via embedded
migrations, each applied once and recorded in `schema_migrations` (instances booting together
wait on an advisory lock). Upgrading a database whose projects kept their original inline
moves each one into the file store at `FILESTORE_ROOT` on the first boot, so boot the new
server with the file store it will serve from; once upgraded, an older server cannot run
against that database. Redis is optional (`REDIS_URL`) — without it the server uses an in-process bus
and is single-instance. Run `go mod vendor` once after cloning (or changing `go.mod`); builds
use the gitignored `server/vendor/` copy.

### Configuration

All keys are documented in [`.env.example`](.env.example):

| Key | Purpose |
|---|---|
| `LISTEN_ADDR`, `TCP_ADDR` | the HTTP/WS and the raw-TCP listen addresses |
| `DATABASE_URL`, `DB_MAX_CONNS`/`DB_MIN_CONNS`/`DB_STATEMENT_TIMEOUT` | Postgres and its pool sizing (timeout in seconds) |
| `REDIS_URL`, `REDIS_POOL_SIZE`/`REDIS_DIAL_TIMEOUT`/`REDIS_IO_TIMEOUT`, `REDIS_SUBSCRIBE_TIMEOUT_SECONDS` | optional Redis fan-out, its client sizing, and how long a new subscription waits for Redis to acknowledge it (default 5) |
| `FILESTORE_ROOT` | where image bytes land; defaults to the relative `./data/filestore` (the server warns at boot — set an absolute path in production) |
| `STORAGE_QUOTA_BYTES` | aggregate cap on stored bytes; an upload past it is refused `507`; `0` = unlimited |
| `STORAGE_QUOTA_PER_OWNER_BYTES` | cap on the bytes one owner session's projects hold together, refused the same way; a project with no owner counts only against the aggregate cap; `0` = unlimited (default) |
| `STORAGE_QUOTA_PER_SESSION_BYTES` | cap on the bytes one session has uploaded, into any project, refused the same way — see [Storage quotas](#storage-quotas); `0` = unlimited (default) |
| `ADMIN_TOKEN`, `AUTH_OPEN`, `TOKEN_TTL_HOURS` | token issuance — see [Security](#security) |
| `CORS_ORIGINS` | allowed browser origins; defaults to loopback only |
| `MAX_BODY_BYTES`, `OP_TIMEOUT_SECONDS` | REST body cap and the store timeout (every handler, the token lookup, the live sessions) |
| `PROJECT_TTL_HOURS`, `EXPIRY_SWEEP_MINUTES`, `SWEEP_BATCH`/`SWEEP_WORKERS` | project expiration and the sweep's sizing — see below |
| `PRESENCE_TTL_SECONDS`, `PRESENCE_HEARTBEAT_SECONDS`, `PRESENCE_SETTLE_MS` | how long another instance trusts this one's live projects (default 60; `0` = off, one instance only), how often they are republished (default 15, at most half the TTL), and how long a burst of joins and leaves settles before a republish (default 500 ms, at most the heartbeat) — see below |
| `FILESTORE_RECONCILE_MINUTES`, `FILESTORE_TMP_MAX_AGE_MINUTES` | the filestore reconcile pass — see below |
| `AUTH_RATE_PER_MINUTE`, `WRITE_RATE_PER_MINUTE`, `HELLO_RATE_PER_MINUTE`, `RATE_BUCKET_IDLE_MINUTES` | the abuse buckets, and how long an untouched one is kept (default 10, at least 1) |
| `RETRY_AFTER_SECONDS`, `LLM_BUSY_RETRY_AFTER_SECONDS` | the `Retry-After` a rate-limited answer carries (60, and 5 over the LLM in-flight cap) |
| `PROJECTS_PAGE_SIZE` | rows in a `GET /projects` that names no `?limit=` (default 100); `0` lists every project |
| `HUB_OUT_BUFFER`, `HUB_OUT_BUDGET_BYTES`, `HELLO_TIMEOUT_SECONDS`, `BUS_SUB_BUFFER` | live-session queues and the hello wait |
| `HUB_NOTICE_TIMEOUT_SECONDS`, `BUS_DROP_WARN_INTERVAL_SECONDS` | the deadline on one goodbye frame — the shutdown notice or a token expiry (default 1, at most `SHUTDOWN_TIMEOUT_SECONDS`) — and the shortest gap between two slow-subscriber drop warnings (default 30) |
| `WS_PING_SECONDS`, `WS_PONG_TIMEOUT_SECONDS`, `TCP_IDLE_TIMEOUT_SECONDS`, `TCP_WRITE_TIMEOUT_SECONDS` | the live transports' keepalive and deadlines |
| `HTTP_READ_HEADER_TIMEOUT_SECONDS`, `HTTP_READ_TIMEOUT_SECONDS`, `HTTP_WRITE_TIMEOUT_SECONDS`, `HTTP_IDLE_TIMEOUT_SECONDS`, `SHUTDOWN_TIMEOUT_SECONDS` | the HTTP server's timeouts and the shutdown drain |
| `TRUSTED_PROXY_CIDRS` | networks whose `X-Forwarded-For` is trusted |
| `TLS_CERT`/`TLS_KEY` | one cert/key secures HTTPS + WSS and the TCP edit channel |
| `LLM_PROVIDER`, `LLM_API_KEY`, `ANTHROPIC_API_KEY`, `LLM_*` | the LLM proxy — see below |

### Storage quotas

All three are off by default and answer an upload past them with the same `507`
(`{"code":"internal","message":"server storage quota exceeded"}`):

- `STORAGE_QUOTA_BYTES` caps everything the file store holds.
- `STORAGE_QUOTA_PER_OWNER_BYTES` caps the projects one session created, whoever uploaded.
- `STORAGE_QUOTA_PER_SESSION_BYTES` caps what one session has uploaded, whichever projects it
  landed in — projects are a shared workspace, so this is the cap that follows a writer.
  Replacing a file moves its bytes to the new uploader; deleting the file or its project, or
  the session expiring, gives them back. Only uploads made while the cap is on count: a
  restart with it off forgets every charge, and bytes stored while it was off are charged to
  no one. Set it the same on every instance.

The admin token cannot upload (it is no session), so it is never charged.

### Project expiration

Each project carries an `expiresAt` (epoch ms; `0`/absent = keep forever). It is off by
default: a project gets an expiry only when a client sets one (the editor's `expire`
command), or when `PROJECT_TTL_HOURS` > 0 stamps `now + TTL` on every new project that
arrives without one. A background sweep runs at startup and every `EXPIRY_SWEEP_MINUTES`
(default 60; `0` disables it): it deletes each expired project and its bytes and broadcasts a
`project-event` (`deleted`) so connected clients drop it live. A project someone is editing in a
live session waits for the next sweep after they leave. The same pass deletes expired session rows.

With several instances on one database, each publishes its live projects and their editor counts
to Postgres every `PRESENCE_HEARTBEAT_SECONDS` and whenever an editor joins or leaves, and the
others trust that list for `PRESENCE_TTL_SECONDS`: a sweep on any instance spares a project
edited on another, and `DELETE /projects/{id}` counts every instance's editors. An instance that
stops lets its list lapse after the TTL, which covers its editors reconnecting elsewhere.

A second pass, at startup and every `FILESTORE_RECONCILE_MINUTES` (default 360; `0` disables
it), removes what a crash or a failed removal left in the file store: a project directory no
project row owns, and upload temp files older than `FILESTORE_TMP_MAX_AGE_MINUTES` (default 60).

## REST API

All routes except `POST /auth/token` require `Authorization: Bearer <token>`.

| Method | Path | Purpose |
|---|---|---|
| POST | `/auth/token` | issue a token+session (gated by the admin token — set or per-boot generated) |
| GET | `/auth/session` | the bearer's own session, `{sessionId, expiresAt}`: the cheap token probe (401 for an unknown or expired token, and for the admin token, which is no session) |
| GET | `/projects` | list project metadata, newest-updated first; `?limit=&after=` paging; `ETag` / `If-None-Match` |
| POST | `/projects` | create a project (optional `expiresAt`); answers with its metadata |
| GET | `/projects/{id}` | full project incl. layout, `{project, layout}` |
| PUT | `/projects/{id}` | update name/color/`expiresAt`/layout under a version guard (409 on conflict); answers with the new metadata |
| DELETE | `/projects/{id}` | delete project + its files |
| GET | `/projects/{id}/files/{kind}` | download bytes; kind = `original` \| `result` \| `video` \| `variant1`..`variant8` \| `chat` |
| POST | `/projects/{id}/files/{kind}?ext=&w=&h=` | upload bytes (the server is codec-free: dimensions are passed in); 507 past any [storage quota](#storage-quotas) |
| DELETE | `/projects/{id}/files/{kind}` | delete one filestore-only kind (`video`/`variantN`/`chat`); idempotent 204 |
| GET | `/llm/info` | LLM proxy status: `{enabled, model}` |
| POST | `/llm/chat` | proxy one chat turn upstream (503 `llmDisabled` without a key; 502 `llmUpstream` with the reason when the upstream fails; 429 `rateLimited` over the spend caps) |
| GET | `/healthz` | liveness |

`GET /projects` returns one page: `?limit=` (1..500) rows, or `PROJECTS_PAGE_SIZE` (100 by
default) when it names none. A full page adds `nextCursor`, an opaque token to send back as
`?after=`; the walk is a keyset over `(updatedAt DESC, id DESC)`, stable while projects change,
and ends on the page with no `nextCursor`. Every page carries an `ETag`; sending it back as
`If-None-Match` answers `304` with no body while the page is unchanged, which is what a poller
wants. List rows never carry `layout`, and neither do the bodies of `POST`/`PUT /projects…`
nor any `project-event`: only `GET /projects/{id}` returns it. The original image is served
only by `GET /projects/{id}/files/original`; no project body carries its bytes.

`POST /projects` still accepts an `originalContent` field — an inline original as a base64
image data URL (`data:image/png;base64,…`) — for older library callers. It is stored exactly
as `POST /projects/{id}/files/original` would store it (same size cap, quotas and
`originalHash`, the data URL's subtype as its extension, `imageW`/`imageH` as its size), and
the created project answers with its `originalPath`. Anything that is not a base64 `image/*`
data URL is refused `400`, and a refused original (a quota, an empty payload) leaves no
project behind.

Every project's metadata — list rows, `GET /projects/{id}`, the create and update answers, a
`project-event` and a `welcome` — carries `originalHash`, the SHA-256 (lowercase hex) of the
stored original, taken as the upload streams to disk. A new original replaces it and a
`result` upload leaves it alone, so equal hashes mean the same picture. It is absent while a
project has no original, and for an original stored before the server recorded it: read that
as "unknown" and reload the picture.

The `video`/`variantN`/`chat` file kinds are filestore-only: the bytes upload and download
through the same routes, but nothing is written to the project record; they are removed with
the project or via the per-file DELETE. `chat` holds the opt-in persisted-chat JSON document
([`contracts/llm/llm-chat.md`](../contracts/llm/llm-chat.md)) and is served as `application/json`.

## LLM proxy

The server can proxy chat turns upstream so the API key never leaves it. It is opt-in via
env; the client-side half — picking `stencil-server` in each front-end — is in the
[root README](../README.md#ai-assistant--setting-up-a-model):

| Key | Default | Meaning |
|---|---|---|
| `LLM_PROVIDER` | `anthropic` | `anthropic` \| `ollama` \| `openai` |
| `LLM_API_KEY` | *(empty)* | upstream credential, any provider. Empty (where one is required) = proxy disabled |
| `ANTHROPIC_API_KEY` | *(empty)* | the older name, honoured **only** when `LLM_PROVIDER=anthropic`; `LLM_API_KEY` wins if both are set |
| `LLM_MODEL` | `claude-opus-5` | default model when a request names none |
| `LLM_BASE_URL` | *(per provider)* | upstream base: Anthropic `https://api.anthropic.com`, Ollama `http://localhost:11434`, OpenAI-compatible `http://localhost:1234/v1` |
| `LLM_MAX_TOKENS` | `32768` | cap; a request's `maxTokens` is clamped to it |
| `LLM_TIMEOUT_SECONDS` | `120` | outbound request timeout |
| `LLM_RATE_PER_MINUTE` | `30` | per-session `/llm/chat` turns per minute; `0` = unlimited |
| `LLM_MAX_IN_FLIGHT` | `8` | concurrent upstream calls server-wide; `0` = unlimited |

Both routes sit behind the usual bearer-token auth, and the proxy enables whenever the
provider is configured — every session token traces back to someone who held the admin
token. Requests are validated before they leave the server (roles `user`/`assistant`, ≤ 32
messages, ≤ 10 images of png/jpeg/webp/gif, ≤ 256 KiB of text, a `[A-Za-z0-9._:-]{1,64}`
model name, the `MAX_BODY_BYTES` cap). With no key, `GET /llm/info` answers
`{"enabled":false,"model":""}` and `POST /llm/chat` answers `503 {"code":"llmDisabled"}`.
The server never logs the key or image payloads.

When the upstream rejects or never answers a call, `/llm/chat` returns
`502 {"code":"llmUpstream","message":…}` whose message names the condition the user can act
on — out of credits, key invalid, model unknown, the upstream's own rate limit, timeout,
unreachable host. Unclassified upstream text is sanitized and capped before it is echoed.

**Spend controls.** Over `LLM_RATE_PER_MINUTE` (per session, burstable up to a minute's
worth) or `LLM_MAX_IN_FLIGHT` (server-wide), `POST /llm/chat` answers
`429 {"code":"rateLimited"}` with `Retry-After`; requests over the in-flight cap are refused
immediately rather than queued. The counters are in-process — a multi-instance deployment
limits per instance, so put a shared limit at the proxy if you run several.

## Live-edit protocol

One JSON `protocol.WSMessage` per WebSocket text frame, or per NDJSON line over TCP. Connect
to `ws://host/ws` or the TCP port; the **first frame must be a `hello`** carrying the token,
optional `clientId`/`name`, and a `projectId`. An empty `projectId` selects the global
`/events` feed instead of a project session. The wire types every client mirrors are in
`internal/protocol`.

Client → server: `hello`, `subscribe`, `edit` (ephemeral op relay), `cursor`, `presence`,
`save` (commit layout, version-guarded), `ping`.
Server → client: `welcome` (snapshot: project metadata, layout, version, peers), `peer-join` /
`peer-leave`, `edit` (relayed), `synced` (commit ack/version), `project-event` (global
feed, project metadata only), `error`, `pong`.

A `hello` naming a project that does not exist is refused with `error` `notFound`. An `edit`
whose `version` (absent = 0) is behind the session's is refused with `badVersion`. A
connection lives no longer than its token: at the session's expiry it gets `error`
`unauthorized` and is closed.

Edits are relayed live without persistence; `save` writes the full layout under the version
guard and broadcasts the new version. A shutdown therefore loses everything since the last
`save` — before closing sessions the server sends every connection an `error` frame with
code `shuttingDown`, so clients can prompt to save and reconnect. A WS keepalive (30 s ping,
10 s pong timeout) reaps half-open peers, so `peer-leave` can lag a dead peer by up to ~40 s.

## Security

- **Tokens** are 256-bit random values; only their SHA-256 hash is stored, compared in
  constant time, and checked for expiry (`TOKEN_TTL_HOURS`, default a week). `POST
  /auth/token` is gated by the admin token: `ADMIN_TOKEN` when set, otherwise a random
  per-boot token printed once at startup. **For anything beyond localhost/dev, set
  `ADMIN_TOKEN`** to a stable secret and **set `CORS_ORIGINS`** to an explicit allowlist of
  your front-end origins (`*` must be asked for explicitly).
- **A token travels as a header, never in a query string.** The one exception is narrow:
  `?token=` is honoured only on an RFC 6455 upgrade request, because the browser `WebSocket`
  API cannot set headers.
- **Open issuance (`AUTH_OPEN=1`)** is an explicit opt-in for trusted networks: `POST
  /auth/token` then mints a token with no bearer at all, and the server prints a loud boot
  warning. `AUTH_RATE_PER_MINUTE` still applies per client IP.
- **WebSocket/TCP connections** must authenticate with a `hello` token before joining any
  session. Failed hellos are metered per client IP (`HELLO_RATE_PER_MINUTE`, default 30) —
  the socket accepts any origin by design, so without that meter a page could try tokens at
  line rate.
- **Authorization is coarse by design: a valid token grants access to every project** —
  read/write/delete over REST, join/edit/save over WS/TCP, and any project's persisted `chat`
  transcript. Treat a token as full access to the whole server and issue tokens only to
  clients you trust with all projects; per-project ACLs are out of scope.
- **The file store** never touches a client-supplied filename: paths are derived from a
  validated project-id allowlist plus a fixed kind, run through `safeJoin` (clean +
  root-prefix re-check + symlink-escape guard), and written atomically. It is path-confined,
  not encrypted: bytes land on disk as uploaded.
- **REST bodies** are size-capped and decoded with unknown-field rejection; the store's
  aggregate size is capped by `STORAGE_QUOTA_BYTES`, one owner session's projects together by
  `STORAGE_QUOTA_PER_OWNER_BYTES`, and what one session uploads by
  `STORAGE_QUOTA_PER_SESSION_BYTES`. Per-session write abuse is metered by
  `WRITE_RATE_PER_MINUTE` (default 120: every project or file create, update, upload and
  delete),
  `AUTH_RATE_PER_MINUTE` (default 10) meters token issuance per client IP. `0` disables
  either.
- **Transport encryption** is opt-in via `TLS_CERT`/`TLS_KEY` (TLS 1.2 minimum) and covers
  HTTPS + WSS and the raw-TCP edit channel. Enable it, or front the server with a
  TLS-terminating proxy, on any untrusted network.
- **Behind a proxy, set `TRUSTED_PROXY_CIDRS`.** Every per-IP limiter keys on the peer
  address, which behind a proxy is the proxy itself. With it set, the client is taken from
  the rightmost `X-Forwarded-For` hop the trusted chain vouched for; empty (the default)
  ignores the header entirely.
- **Every REST handler** runs its store calls under `OP_TIMEOUT_SECONDS` (default 10), the
  per-request token lookup and the live sessions included, so one stuck query cannot hold a
  pool connection for the whole write timeout.

## Tests

```bash
go test ./...                       # everything but store/ + redisbus/ is offline
go test -race ./internal/hub/...
go test -run XXX -bench . ./internal/...   # opt-in benchmarks
```

The integration tests in `store/` and `redisbus/` **self-skip** when `TEST_DATABASE_URL` /
`REDIS_URL` are unset or unreachable. The store tests read `TEST_DATABASE_URL`, never
`DATABASE_URL`: their setup **truncates** the named database, so point them at a throwaway
one:

```bash
export TEST_DATABASE_URL='postgres://...stencil_test?sslmode=disable'
export REDIS_URL='redis://localhost:6379/15'
go test ./...
```
