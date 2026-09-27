# Stencil LLM contract — providers & wire mappings (§5–§6)

Part of the [Stencil LLM contract](llm-contract.md); section numbers continue the
root document's (code comments cite `§5`/`§6.x` everywhere). The machine-readable
constants — default base URLs, wire paths, timeouts, the server's Anthropic upstream
constants — live in
[`browser/js/config/llm/providers.json`](../../browser/js/config/llm/providers.json), the
normative asset for this file's numbers (guarded by `desktop/tests/support/theme/configCanon.headless.cpp`,
bot `ProvidersAssetTests`, `pystencil/tests/test_canonical_drift.py`, and the cli/mcp
compile-time embeds). Wire/error behaviour is pinned by the conformance fixtures
`browser/js/config/llm/fixtures/providerWire/` and `fixtures/sanitizer/` (see their
`_schema.md`), walked by every surface's fixture tests.

## 5. Provider configuration

Identical shape in every client (naming adapted to each language's conventions):

```json
{
  "provider":  "ollama" | "openai-compat" | "anthropic" | "stencil-server",
  "baseUrl":   "http://localhost:11434",
  "model":     "llama3.2-vision",
  "apiKey":    "",
  "serverUrl": "https://stencil.example.com:8090"
}
```

- Clients MAY additionally offer a local-only `provider` value **`none`** ("assistant
  off"): the assistant UI is disabled, nothing is probed or sent anywhere, and `none`
  never appears on any wire — it is a client-side switch, not a §6 mapping.
- `baseUrl`, `model`, `apiKey` apply to `ollama` / `openai-compat` / `anthropic`. On
  `openai-compat` `apiKey` is optional (LM Studio needs none) and is sent as
  `Authorization: Bearer <apiKey>`; on `anthropic` it is required, it is the user's own
  Anthropic API key, it is sent as `x-api-key` (§6.5), and it is a **session key** (below).
- `serverUrl` applies to `stencil-server` only: which configured Stencil collaboration
  server proxies the upstream. The client authenticates with its **existing** bearer
  token for that server. `model` may be empty (server default).

**Defaults** (pre-filled on first run; the user can edit every URL). Normative values:
`providers.json` → `providers.<name>`; as recorded there today:

| Provider | Default `baseUrl` / `serverUrl` | Default `model` |
|---|---|---|
| `ollama` | `http://localhost:11434` | empty — user picks (e.g. `llama3.2-vision`) |
| `openai-compat` (LM Studio, llama.cpp, vLLM, …) | `http://localhost:1234/v1` | empty — server serves whatever is loaded |
| `anthropic` | `https://api.anthropic.com` | empty — `serverDefaults.model`, the same default the server's upstream uses |
| `stencil-server` | the client's first already-configured Stencil server connection (`defaultBaseUrl: null` in the asset) | empty — server-side default |

**Timeouts** are `providers.json` → `timeouts`: chat requests 120 s, provider probes
3000 ms — with two per-surface entries the asset records explicitly rather than papering
over:

- **cli**: `chatSeconds: 600` — the console waiter allows long local-model runs;
  unification to 120 is deliberately deferred (the change is one constant,
  `request_timeout_ms` in `cli/src/llm/transport.zig`, re-exported by the `src/llm.zig` façade).
- **browser**: `chatSeconds: null` — abort-driven (the user's stop button), no fixed
  chat deadline.

Per-client persistence of overrides:

| Client | Storage | Keys |
|---|---|---|
| browser | localStorage | `drawingApp_llmSettings` (JSON of the shape above) |
| desktop | fileStore Settings JSON | `llmProvider`, `llmBaseUrl`, `llmModel`, `llmApiKey`, `llmServerUrl` |
| pystencil | `LlmConfig(...)` args, env fallback | `STENCIL_LLM_PROVIDER`, `STENCIL_LLM_BASE_URL`, `STENCIL_LLM_MODEL`, `STENCIL_LLM_API_KEY`, `STENCIL_LLM_SERVER_URL`; its console mirrors the CLI console's `/prompt`, `/p`, and `/llm …` commands |
| bot | env/.env via `BotOptions` | same `STENCIL_LLM_*` names as pystencil, plus `STENCIL_BOT_ALLOWED_USERS` — the bot's global, fail-closed allowlist of Telegram user ids (empty = off for everyone; it gates every command, not only the assistant, because one shared key serves every chat) — and `STENCIL_LLM_SERVER_TOKEN`, the operator's bearer for a pinned `STENCIL_LLM_SERVER_URL`, used when the invoking user has no `/connect` of their own to it (the cli console's rule) |
| mcp | env/.env via `config.rs` | same `STENCIL_LLM_*` names, plus `STENCIL_LLM_SERVER_TOKEN` (mcp has no connection store). `stencil_prompt` takes a per-call `model` only — the provider and endpoint are not caller-settable, and its plain-http transport sends credentials to loopback peers only |
| cli (console) | `STENCIL_LLM_*` env (same names, incl. `STENCIL_LLM_SERVER_TOKEN`) as initial values | in-session overrides via `/llm provider|url|model|key|server` console commands; stencil-server auth reuses the console's `/connect` token when URLs match |
| extension | `chrome.storage` | `llmSettings` (JSON of the §5 shape) |

**The `anthropic` key is a session key.** It reaches Anthropic straight from the client,
with no Stencil server in between, so it is held only as long as the session that took it,
and at most `providers.anthropic.sessionKey.ttlMinutes` (720) after it was entered:

| Client | Where the key lives | Gone when |
|---|---|---|
| browser | `sessionStorage` (`stencil_llm_session_key`: the key and its expiry) | the tab closes, the TTL passes, or the user forgets it; a reload of the same tab keeps it |
| extension | `chrome.storage.session` (trusted contexts only) | the browser closes, the extension reloads, the TTL passes, or the user forgets it |
| desktop | process memory | the app quits, the TTL passes, or the user forgets it |
| cli and pystencil consoles | process memory, from `/llm key` (hidden input) or `STENCIL_LLM_API_KEY` | the console exits, the TTL passes, or `/llm key forget` |
| cli one-shot, pystencil library | process memory, from `STENCIL_LLM_API_KEY` or `LlmConfig(api_key=…)` | the process ends or the TTL passes |

mcp (a plain-http transport) and the bot (an operator service) do not offer `anthropic`.

It is never written to localStorage, a settings file, the connection store, a project, an
export, a hand-off fragment, a URL or a log; the settings shapes above persist everything
for `anthropic` except the key. An expired key is dropped before the next request, which
then fails as a missing key so the UI asks for it again.

Note for browser users calling local providers directly: Ollama/LM Studio must allow the
app's origin (Ollama `OLLAMA_ORIGINS`, LM Studio "enable CORS"). Documented in the
settings UI help text.

## 6. Wire mappings

All chat requests are `POST`, `Content-Type: application/json`, non-streaming (v1). Paths
below are `providers.json` → `providers.<name>.chatPath` / `infoPath`; §6.4's model list
and probe take theirs from `modelsPath` / `probePath`. A client selects a mapping by the
provider's `wire` (`ollama`, `openai`, `anthropic`, `server`), never by its name. No request
on any wire — chat, model list, probe or `/llm/info` — follows a redirect: a key or bearer
token must not ride a 30x to a second host, so a 30x fails the request.

### 6.1 `ollama` — native chat

`POST {baseUrl}/api/chat`

```json
{
  "model": "<model>",
  "stream": false,
  "messages": [
    {"role": "system", "content": "<system prompt>"},
    {"role": "user", "content": "<text>", "images": ["<base64>", "..."]}
  ]
}
```

Reply text = `message.content`.

### 6.2 `openai-compat` — LM Studio & friends

`POST {baseUrl}/chat/completions` (the configured `baseUrl` already ends in `/v1`).
Optional `Authorization: Bearer <apiKey>`.

```json
{
  "model": "<model>",
  "stream": false,
  "messages": [
    {"role": "system", "content": "<system prompt>"},
    {"role": "user", "content": [
      {"type": "text", "text": "<text>"},
      {"type": "image_url", "image_url": {"url": "data:image/png;base64,<b64>"}}
    ]}
  ]
}
```

Reply text = `choices[0].message.content`.

### 6.3 `stencil-server` — Anthropic proxy

`POST {serverUrl}/llm/chat`, `Authorization: Bearer <existing Stencil session token>`.
DTOs live in `server/internal/protocol/protocol.go` and are re-declared by clients (the
same mirror rule as the rest of the protocol package):

```json
// request (protocol.LlmChatRequest)
{
  "system": "<system prompt>",
  "messages": [
    {"role": "user", "text": "<text>", "images": [{"mediaType": "image/png", "data": "<b64>"}]},
    {"role": "assistant", "text": "<prior reply>"}
  ],
  "model": "",          // optional; empty = server default
  "maxTokens": 0        // optional; clamped server-side
}

// response (protocol.LlmChatResponse)
{ "model": "claude-opus-5", "text": "<raw LLM text>", "stopReason": "end_turn" }
```

`GET {serverUrl}/llm/info` → `{"enabled": true, "model": "claude-opus-5"}` — lets
settings UIs render "via server X (model)". When the server has no upstream credential,
`/llm/chat` answers `503 {"code":"llmDisabled","message":…}`.

**Upstream failures say WHY** (the sanitizer rules; cases pinned by
`fixtures/sanitizer/cases.json`). A proxied call that the upstream rejects answers
`502 {"code":"llmUpstream","message":…}` whose message names the actual condition in
the user's terms — out of credits, key rejected, unknown model, upstream timed out,
unreachable host — because every one of those is the user's to fix and none of them
is a secret. A generic "LLM request failed" is not acceptable: it sends the user
hunting through server logs for a fact the server already had. The message is
SANITIZED — never the API key (or any fragment of it), never request bodies, image
data, or internal URLs.

**Say the reason once.** When the condition IS recognised, the message is the short
reason alone — no HTTP status, and none of the upstream's own prose. "out of credits
or no active billing" is the whole fact; appending *"Your credit balance is too low
to access the Anthropic API. Please go to Plans & Billing to upgrade or purchase
credits."* restates it at four times the length and buries it in a chat bubble or a
console line. Only an UNRECOGNISED failure carries the upstream's short text
(truncated ≤ 200 chars) plus its status, because there the provider's words are the
only information available. The full envelope always stays in the server log. Clients
render `message` as the chat error and MAY special-case the code for a
settings/billing affordance; an unknown `code` still renders its `message`.

`stopReason` values: `end_turn` (normal), `max_tokens` (truncated — client shows a
"response truncated" note and does NOT parse a plan from it), `refusal` (client shows
the refusal as a chat error; never parsed as a plan).

**Typed errors everywhere.** Across all three mappings, a 2xx body outside the
documented shape (no `message.content` / `choices[0].message.content` / `text`
string) is a typed **bad-reply** error on every client — never a silent empty-string
reply (an empty string that IS present stays a blank reply). Likewise the server's
`503 llmDisabled` is a typed **disabled** error, distinct from generic HTTP
failures, so clients can render a configure hint instead of a transport error.
(`fixtures/providerWire/httpErrors.json` pins the classification.)

**The server proxies any of the three §6 mappings.** `LLM_PROVIDER` picks which
upstream it speaks — `anthropic` (default), `ollama`, or `openai-compat` — using the
SAME request/response translations defined in §6.1/§6.2/§6.3, so a deployment can put
a local Ollama or an OpenAI-compatible server behind the same bearer-authenticated
`/llm/chat` route. From a client's point of view nothing changes: it still selects the
`stencil-server` provider and never learns which upstream sits behind it. Non-Anthropic
upstreams map their reply onto the same `stopReason` vocabulary (OpenAI `length` ⇒
`max_tokens`, `content_filter` ⇒ `refusal`, otherwise `end_turn`).

Server-side env keys (in `server/internal/config`): `LLM_PROVIDER` (default
`anthropic`; an unknown value leaves the proxy disabled), `LLM_API_KEY` (the upstream
credential for any provider; required by `anthropic` only — empty ⇒ disabled, local
providers need none) with `ANTHROPIC_API_KEY` as a legacy alias honoured **only** for
`anthropic`, `LLM_RATE_PER_MINUTE` (default 30, per session) and `LLM_MAX_IN_FLIGHT`
(default 8, server-wide) bounding what one token can spend — over either, `/llm/chat`
answers `429 {"code":"rateLimited"}` — `LLM_MODEL`
(default `claude-opus-5`), `LLM_BASE_URL` (defaults per provider: Anthropic
`https://api.anthropic.com`, Ollama `http://localhost:11434`, OpenAI-compatible
`http://localhost:1234/v1`), `LLM_MAX_TOKENS` (default 32768), `LLM_TIMEOUT_SECONDS`
(default 120). Token issuance is never silently open next to a usable upstream: an unset
`ADMIN_TOKEN` gets a random per-boot token (printed once at startup), it is not ignored.
The server calls
`POST {LLM_BASE_URL}/v1/messages` with headers `x-api-key`,
`anthropic-version: 2023-06-01` (the `providers.json` → `anthropicUpstream` constants);
images become `{"type":"image","source":{"type":"base64","media_type":…,"data":…}}`
blocks; the reply is the concatenation of the response's `content[]` text blocks.

### 6.4 Model list and reachability probe

Both are a `GET {base}{path}` under the `timeouts.probeMs` deadline, with the same
authorization the wire's chat sends (none for `ollama`, the optional key for `openai`, the
session bearer for `server`, the §6.5 headers for `anthropic`); `{base}` is `baseUrl`, or
`serverUrl` for `stencil-server`.
Neither ever spends chat tokens, and neither ever throws to its caller.

| `wire` | `modelsPath` → model names | `probePath` → probe detail |
|---|---|---|
| `ollama` | `/api/tags` → `models[].name` | `/api/version` → `v<version>` |
| `openai` | `/models` → `data[].id` | `/models` → the first `data[].id` |
| `server` | `/llm/info` → `[model]` when set | `/llm/info` → `model`; `enabled: false` is a failed probe ("LLM disabled on this server") |
| `anthropic` | `/v1/models` → `data[].id` | `/v1/models` → the first `data[].id`; no key is a failed probe ("no API key for this session") |

A non-2xx answer is an empty model list and a failed probe reading `HTTP <status>`; a
2xx body that is not JSON reads as `{}`. The model list only suggests: the model field
stays free-form.

### 6.5 `anthropic` — direct, with a session key

`POST {baseUrl}/v1/messages` straight to Anthropic, no Stencil server in between. Headers:
`x-api-key: <apiKey>` (the §5 session key), `anthropic-version` (`providers.json` →
`anthropicUpstream.version`), `Content-Type: application/json`, and — from a web page or
an extension page only — `anthropic-dangerous-direct-browser-access: true`, without which
Anthropic refuses a browser origin. No `Authorization` header. The body is exactly the
server's upstream body (§6.3), so a direct turn and a proxied one reach Anthropic alike:

```json
{
  "model": "<model, or serverDefaults.model when empty>",
  "max_tokens": 32768,
  "system": "<system prompt>",
  "messages": [
    {"role": "user", "content": [
      {"type": "text", "text": "<text>"},
      {"type": "image", "source": {"type": "base64", "media_type": "image/png", "data": "<b64>"}}
    ]},
    {"role": "assistant", "content": [{"type": "text", "text": "<prior reply>"}]}
  ]
}
```

`max_tokens` is `serverDefaults.maxTokens`; a turn's text block comes first, then its images
in order; a turn with empty text sends only its images. Reply text = the concatenation of
the response's `content[]` blocks whose `type` is `text`. `stop_reason` maps as the
server's `stopReason` does (§6.3): `max_tokens` is a **truncated** error, `refusal` a
**refusal** error, anything else a reply; a 2xx body without a `content` array is a
**bad-reply** error.

A non-2xx answer is an **http** error whose message says WHY, by the server's own rules
(§6.3, `server/internal/llm/upstream.go`): the Anthropic envelope
`{"type":"error","error":{"type","message"}}` and the status classify into one reason —

| Condition | Message |
|---|---|
| billing (`credit balance`, `billing`, `insufficient_quota`, HTTP 402) | the LLM provider is out of credits or has no active billing |
| the key (`authentication_error`, `permission_error`, HTTP 401/403) | the LLM provider rejected the API key |
| the model (`not_found_error`, `model not found`, HTTP 404) | the LLM provider does not have the requested model |
| `rate_limit_error`, HTTP 429 | the LLM provider is rate-limiting this key |
| HTTP 408/504 | the LLM provider did not respond in time |
| `overloaded_error`, `api_error`, HTTP 5xx | the LLM provider is temporarily unavailable |
| anything else | the LLM provider returned an error (HTTP n): <sanitized upstream text> |

The sanitized text follows `fixtures/sanitizer/`, and the client applies the server's
secret-fragment veto with its own session key: upstream text containing any 8-character
run of the key is dropped. A request with no key (never entered, expired or forgotten) is
not sent; it fails as a **disabled** error reading "no API key for this session", so the
UI asks for the key again.

**The key travels over https.** Plain `http` is allowed only to a loopback host
(`localhost`, `127.0.0.0/8`, `::1`) — a local mock or proxy the user named; for any other
host nothing is sent, and the request fails as a **disabled** error reading
`refusing to send the API key to '<host>' over plain http — use https`. Redirects are never
followed, so the key cannot ride a 30x to a second host.
