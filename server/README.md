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
migrations. Redis is optional (`REDIS_URL`) — without it the server uses an in-process bus
and is single-instance. Run `go mod vendor` once after cloning (or changing `go.mod`); builds
use the gitignored `server/vendor/` copy.

### Configuration

All keys are documented in [`.env.example`](.env.example):

| Key | Purpose |
|---|---|
| `LISTEN_ADDR`, `TCP_ADDR` | the HTTP/WS and the raw-TCP listen addresses |
| `DATABASE_URL`, `DB_MAX_CONNS`/`DB_MIN_CONNS`/`DB_STATEMENT_TIMEOUT` | Postgres and its pool sizing (timeout in seconds) |
| `REDIS_URL`, `REDIS_POOL_SIZE`/`REDIS_DIAL_TIMEOUT`/`REDIS_IO_TIMEOUT` | optional Redis fan-out and its client sizing |
| `FILESTORE_ROOT` | where image bytes land; defaults to the relative `./data/filestore` (the server warns at boot — set an absolute path in production) |
| `STORAGE_QUOTA_BYTES` | aggregate cap on stored bytes; an upload past it is refused `507`; `0` = unlimited |
| `ADMIN_TOKEN`, `AUTH_OPEN`, `TOKEN_TTL_HOURS` | token issuance — see [Security](#security) |
| `CORS_ORIGINS` | allowed browser origins; defaults to loopback only |
| `MAX_BODY_BYTES`, `OP_TIMEOUT_SECONDS` | REST body cap and the per-handler store timeout |
| `PROJECT_TTL_HOURS`, `EXPIRY_SWEEP_MINUTES` | project expiration — see below |
| `AUTH_RATE_PER_MINUTE`, `WRITE_RATE_PER_MINUTE`, `HELLO_RATE_PER_MINUTE` | the abuse buckets |
| `TRUSTED_PROXY_CIDRS` | networks whose `X-Forwarded-For` is trusted |
| `TLS_CERT`/`TLS_KEY` | one cert/key secures HTTPS + WSS and the TCP edit channel |
| `LLM_PROVIDER`, `LLM_API_KEY`, `ANTHROPIC_API_KEY`, `LLM_*` | the LLM proxy — see below |

### Project expiration

Each project carries an `expiresAt` (epoch ms; `0`/absent = keep forever). It is off by
default: a project gets an expiry only when a client sets one (the editor's `expire`
command), or when `PROJECT_TTL_HOURS` > 0 stamps `now + TTL` on every new project that
arrives without one. A background sweep runs at startup and every `EXPIRY_SWEEP_MINUTES`
(default 60; `0` disables it): it deletes each expired project and its bytes and broadcasts a
`project-event` (`deleted`) so connected clients drop it live.

## REST API

All routes except `POST /auth/token` require `Authorization: Bearer <token>`.

| Method | Path | Purpose |
|---|---|---|
| POST | `/auth/token` | issue a token+session (gated by the admin token — set or per-boot generated) |
| GET | `/projects` | list project metadata, newest-updated first; optional `?limit=&after=` paging |
| POST | `/projects` | create a project (optional `expiresAt`) |
| GET | `/projects/{id}` | full project incl. layout + original content |
| PUT | `/projects/{id}` | update name/color/`expiresAt`/layout under a version guard (409 on conflict) |
| DELETE | `/projects/{id}` | delete project + its files |
| GET | `/projects/{id}/files/{kind}` | download bytes; kind = `original` \| `result` \| `video` \| `variant1`..`variant8` \| `chat` |
| POST | `/projects/{id}/files/{kind}?ext=&w=&h=` | upload bytes (the server is codec-free: dimensions are passed in); 507 past `STORAGE_QUOTA_BYTES` |
| DELETE | `/projects/{id}/files/{kind}` | delete one filestore-only kind (`video`/`variantN`/`chat`); idempotent 204 |
| GET | `/llm/info` | LLM proxy status: `{enabled, model}` |
| POST | `/llm/chat` | proxy one chat turn upstream (503 `llmDisabled` without a key; 502 `llmUpstream` with the reason when the upstream fails; 429 `rateLimited` over the spend caps) |
| GET | `/healthz` | liveness |

`GET /projects` returns every project by default. Paging is opt-in: pass `?limit=` (1..500)
and the response adds `nextCursor`, an opaque token to send back as `?after=`; the walk is a
keyset over `(updatedAt DESC, id DESC)`, stable while projects change, and ends on the page
with no `nextCursor`. List rows never carry `layout` or `originalContent`.

The `video`/`variantN`/`chat` file kinds are filestore-only: the bytes upload and download
through the same routes, but nothing is written to the project record; they are removed with
the project or via the per-file DELETE. `chat` holds the opt-in persisted-chat JSON document
([`llm-contract/llm-chat.md`](../llm-contract/llm-chat.md)) and is served as `application/json`.

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
Server → client: `welcome` (snapshot: project, layout, version, peers), `peer-join` /
`peer-leave`, `edit` (relayed), `synced` (commit ack/version), `project-event` (global
feed), `error`, `pong`.

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
  aggregate size is capped by `STORAGE_QUOTA_BYTES`. Per-session write abuse is metered by
  `WRITE_RATE_PER_MINUTE` (default 120: project creations and file uploads),
  `AUTH_RATE_PER_MINUTE` (default 10) meters token issuance per client IP. `0` disables
  either.
- **Transport encryption** is opt-in via `TLS_CERT`/`TLS_KEY` (TLS 1.2 minimum) and covers
  HTTPS + WSS and the raw-TCP edit channel. Enable it, or front the server with a
  TLS-terminating proxy, on any untrusted network.
- **Behind a proxy, set `TRUSTED_PROXY_CIDRS`.** Every per-IP limiter keys on the peer
  address, which behind a proxy is the proxy itself. With it set, the client is taken from
  the rightmost `X-Forwarded-For` hop the trusted chain vouched for; empty (the default)
  ignores the header entirely.
- **Every REST handler** runs its store calls under `OP_TIMEOUT_SECONDS` (default 10), so
  one stuck query cannot hold a pool connection for the whole write timeout.

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
