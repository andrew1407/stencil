# Changelog — Stencil collaboration server

The version here is the app's own; the release of Stencil as a whole it shipped in, and
every other app's version at that release, are in the [repository changelog](../CHANGELOG.md).

## [1.0.0]

Shipped in Stencil 1.0.0, the first release. A Go server that stores and shares projects.

### Added

- A REST API for projects (create, list with keyset paging and `ETag`s, read, update under a
  version guard, delete) and their files: the original, the result, a video, up to eight
  variants and a saved chat.
- A live events feed over WebSocket and raw TCP, fanned out across instances through Redis
  when configured.
- Postgres for metadata with embedded migrations, and a path-confined file store with
  atomic writes and content hashes.
- Token sessions: hashed 256-bit tokens with expiry, an admin-gated or explicitly open
  issuer, and revocation of one session or all.
- An LLM proxy for Anthropic, Ollama or an OpenAI-compatible upstream that keeps the key on
  the server, with request validation, per-session rate limits and an in-flight cap.
- Storage quotas in aggregate, per owner and per session; project expiry with a background
  sweep; a file-store reconcile pass.
- Rate limits and connection caps per client IP, body caps, operation timeouts,
  trusted-proxy handling, CORS allowlists and optional TLS on every listener.
- A Docker image and a `docker-compose.yml` for the full local stack.
