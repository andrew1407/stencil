# Stencil LLM contract — providers & wire mappings (§5–§6)

Part of the [Stencil LLM contract](llm-contract.md); section numbers continue the
root document's (code comments cite `§5`/`§6.x` everywhere). The machine-readable
constants — default base URLs, wire paths, timeouts, the server's Anthropic upstream
constants — live in
[`browser/js/config/llm/providers.json`](../browser/js/config/llm/providers.json), the
normative asset for this file's numbers (guarded by `desktop/tests/configCanon.headless.cpp`,
bot `ProvidersAssetTests`, `pystencil/tests/test_canonical_drift.py`, and the cli/mcp
compile-time embeds). Wire/error behaviour is pinned by the conformance fixtures
`browser/js/config/llm/fixtures/providerWire/` and `fixtures/sanitizer/` (see their
`_schema.md`), walked by every surface's fixture tests.

## 5. Provider configuration

Identical shape in every client (naming adapted to each language's conventions):

```json
{
  "provider":  "ollama" | "openai-compat" | "stencil-server",
  "baseUrl":   "http://localhost:11434",
  "model":     "llama3.2-vision",
  "apiKey":    "",
  "serverUrl": "https://stencil.example.com:8090"
}
```

- Clients MAY additionally offer a local-only `provider` value **`none`** ("assistant
  off"): the assistant UI is disabled, nothing is probed or sent anywhere, and `none`
  never appears on any wire — it is a client-side switch, not a §6 mapping.
- `baseUrl`, `model`, `apiKey` apply to `ollama` / `openai-compat`. `apiKey` is optional
  (LM Studio needs none) and is sent as `Authorization: Bearer <apiKey>` on
  `openai-compat` only.
- `serverUrl` applies to `stencil-server` only: which configured Stencil collaboration
  server proxies the upstream. The client authenticates with its **existing** bearer
  token for that server. `model` may be empty (server default).

**Defaults** (pre-filled on first run; the user can manually edit every URL). The
normative values are `providers.json` → `providers.<name>.defaultBaseUrl` /
`displayName`; as recorded there today:

| Provider | Default `baseUrl` / `serverUrl` | Default `model` |
|---|---|---|
| `ollama` | `http://localhost:11434` | empty — user picks (e.g. `llama3.2-vision`) |
| `openai-compat` (LM Studio, llama.cpp, vLLM, …) | `http://localhost:1234/v1` | empty — server serves whatever is loaded |
| `stencil-server` | the client's first already-configured Stencil server connection (`defaultBaseUrl: null` in the asset) | empty — server-side default |

**Timeouts** are `providers.json` → `timeouts`: chat requests 120 s, provider probes
3000 ms — with two per-surface entries the asset records explicitly rather than papering
over:

- **cli**: `chatSeconds: 600` — the console waiter allows long local-model runs;
  unification to 120 is deliberately deferred (the change is one constant,
  `cli/src/llm.zig` `request_timeout_ms`).
- **browser**: `chatSeconds: null` — abort-driven (the user's stop button), no fixed
  chat deadline.

Per-client persistence of overrides:

| Client | Storage | Keys |
|---|---|---|
| browser | localStorage | `drawingApp_llmSettings` (JSON of the shape above) |
| desktop | fileStore Settings JSON | `llmProvider`, `llmBaseUrl`, `llmModel`, `llmApiKey`, `llmServerUrl` |
| pystencil | `LlmConfig(...)` args, env fallback | `STENCIL_LLM_PROVIDER`, `STENCIL_LLM_BASE_URL`, `STENCIL_LLM_MODEL`, `STENCIL_LLM_API_KEY`, `STENCIL_LLM_SERVER_URL`; its console mirrors the CLI console's `/prompt`, `/p`, and `/llm …` commands |
| bot | env/.env via `BotOptions` | same `STENCIL_LLM_*` names as pystencil, plus `STENCIL_BOT_ALLOWED_USERS` — the Telegram user ids allowed to use the assistant at all (empty = off for everyone), since one shared key serves every chat — and `STENCIL_LLM_SERVER_TOKEN`, the operator's bearer for a pinned `STENCIL_LLM_SERVER_URL`, used when the invoking user has no `/connect` of their own to it (the cli console's rule) |
| mcp | env/.env via `config.rs` | same `STENCIL_LLM_*` names, plus `STENCIL_LLM_SERVER_TOKEN` (mcp has no connection store). `stencil_prompt` takes a per-call `model` only — the provider and endpoint are not caller-settable, and its plain-http transport sends credentials to loopback peers only |
| cli (console) | `STENCIL_LLM_*` env (same names, incl. `STENCIL_LLM_SERVER_TOKEN`) as initial values | in-session overrides via `/llm provider|url|model|key|server` console commands; stencil-server auth reuses the console's `/connect` token when URLs match |
| extension | `chrome.storage` | `llmSettings` (JSON of the §5 shape) |

Note for browser users calling local providers directly: Ollama/LM Studio must allow the
app's origin (Ollama `OLLAMA_ORIGINS`, LM Studio "enable CORS"). Documented in the
settings UI help text.

## 6. Wire mappings

All requests are `POST`, `Content-Type: application/json`, non-streaming (v1). Paths
below are `providers.json` → `providers.<name>.chatPath` / `infoPath`.

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
(default 120). `ADMIN_TOKEN` must be set for ANY provider — open token issuance plus a
usable upstream means anyone could spend it. The server calls
`POST {LLM_BASE_URL}/v1/messages` with headers `x-api-key`,
`anthropic-version: 2023-06-01` (the `providers.json` → `anthropicUpstream` constants);
images become `{"type":"image","source":{"type":"base64","media_type":…,"data":…}}`
blocks; the reply is the concatenation of the response's `content[]` text blocks.
