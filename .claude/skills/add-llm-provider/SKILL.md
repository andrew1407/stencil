---
name: add-llm-provider
description: >-
  The file-by-file procedure for adding an LLM provider (a new wire, or a new preset over an
  existing one) to Stencil — providers.json, the provider contract, each surface's HTTP
  client mapping, the settings UIs and the wire fixtures. Use when asked to support another
  model API or endpoint shape, or to change how a provider's requests or responses map.
---

# Add an LLM provider

1. `common/config/llm/providers.json` — id, `displayName`, `defaultBaseUrl`, `chatPath`,
   `modelsPath`, `probePath`, `wire`. Then `node tools/syncTwins.mjs` — the extension,
   pystencil and the server ship copies.
2. `contracts/llm/llm-providers.md` — the wire mapping, normatively.
3. The mapping in each client, using its platform's built-in HTTP (**no new dependency**):
   browser `browser/js/llm/client.js`; extension `browser-extension/src/llm/client.js` (a
   port of the browser's — `syncTwins` carries it); desktop
   `desktop/src/llm/client/LlmClient.cpp`; cli `cli/src/llm/wire.zig` +
   `cli/src/llm/transport.zig`; mcp `mcp/src/llmtransport/`; bot, the `Infrastructure` ring,
   `Infrastructure/Llm/HttpLlmClient.cs`; pystencil `pystencil/pystencil/llm/wire.py` (the
   table; `client.py` is the transport); server `server/internal/llm/`.
4. Settings UI per surface (browser `browser/js/llm/settings.js`, desktop
   `desktop/src/dialogs/settings/LlmSettingsForm.cpp`, extension
   `browser-extension/src/llm/settings.js`, cli `/llm`).
5. Fixtures under `common/fixtures/llm/providerWire/`, plus each surface's walker.
6. An endpoint is **always explicit user configuration** — never discovered from fetched or
   scanned content. Keys live in env, never in a URL. See `.claude/rules/security.md`.
