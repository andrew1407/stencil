from __future__ import annotations

"""Provider configuration (contract §5): the provider list, their default base URLs,
the attachment caps, and :class:`LlmConfig` — env-loaded, never discovered.
"""

import importlib.resources
import json
import os
from dataclasses import dataclass

from .._types import NoneType
from .errors import LlmError

MAX_ATTACHMENTS = 3

# The console's §2.1 upload set: how many /upload-ed images one turn may index with an
# `image` op (the cli console's max_attachments). Past it the oldest upload falls off.
MAX_UPLOAD_ATTACHMENTS = 8

# Media types the contract accepts for attached images (§7).
ACCEPTED_MEDIA_TYPES = ("image/png", "image/jpeg", "image/webp", "image/gif")

# Every LLM call is bounded by pystencil.server's _LLM_TIMEOUT (the asset's
# timeouts.chatSeconds) so a hostile/slow/hung provider can't block the caller.

# Providers + their pre-filled default base URLs (contract §5), from the checked-in
# copy of the canonical providers asset (browser/js/config/llm/providers.json;
# tests/test_canonical_drift.py byte-pins the copy). stencil-server's null default
# drops out of DEFAULT_BASE_URLS — it uses server_url + the existing bearer token.
_PROVIDERS_ASSET = json.loads(
  importlib.resources.files("pystencil")
  .joinpath("_data/providers.json")
  .read_text(encoding="utf-8")
)
PROVIDERS = tuple(_PROVIDERS_ASSET["providers"])
DEFAULT_BASE_URLS = {
  name: p["defaultBaseUrl"]
  for name, p in _PROVIDERS_ASSET["providers"].items()
  if p["defaultBaseUrl"]
}


# ── provider configuration (contract §5) ──────────────────────────────────────
@dataclass
class LlmConfig:
  """Which LLM endpoint to talk to — the same shape every client shares.

  ``base_url``/``model``/``api_key`` apply to ``ollama``/``openai-compat`` (the key
  is optional and sent as a Bearer header on ``openai-compat`` only); ``server_url``
  applies to ``stencil-server`` only (the collaboration server proxying Anthropic,
  authenticated with the existing session token). An empty ``base_url`` is pre-filled
  with the provider's contract default.
  """

  provider: str = "ollama"
  base_url: str = ""
  model: str = ""
  api_key: str = ""
  server_url: str = ""

  def __post_init__(self) -> None:
    self.provider = (self.provider or "ollama").strip().lower()
    if self.provider not in PROVIDERS:
      raise ValueError(
        "unknown LLM provider %r — use one of: %s"
        % (self.provider, ", ".join(PROVIDERS))
      )
    if not self.base_url: self.base_url = DEFAULT_BASE_URLS.get(self.provider, "")
    # True once a caller pins an explicit base URL via set_base_url();
    # set_provider() then keeps it instead of re-filling the provider default.
    self._url_pinned = False

  def set_base_url(self, url: str) -> "LlmConfig":
    """Pin an explicit base URL (a user override): :meth:`set_provider` keeps a
    pinned URL across provider switches instead of re-filling the default."""
    self.base_url = url
    self._url_pinned = True
    return self

  def set_provider(
    self, provider: str, *, keep_url: (bool | NoneType) = None
  ) -> "LlmConfig":
    """Switch providers in place, managing the default base URL (this module owns
    :data:`DEFAULT_BASE_URLS`).

    Unless the URL is kept, ``base_url`` is re-filled with the new provider's
    contract default. ``keep_url`` defaults to whether the current URL was pinned
    via :meth:`set_base_url`; pass an explicit bool to override. An unknown
    provider raises ``ValueError`` and changes nothing.
    """
    p = (provider or "").strip().lower()
    if p not in PROVIDERS:
      raise ValueError(
        "unknown LLM provider %r — use one of: %s" % (provider, ", ".join(PROVIDERS))
      )
    self.provider = p
    if not (self._url_pinned if keep_url is None else keep_url):
      self.base_url = DEFAULT_BASE_URLS.get(p, "")
    return self

  @classmethod
  def from_env(cls, env: (dict | NoneType) = None) -> "LlmConfig":
    """Build a config from the ``STENCIL_LLM_*`` environment keys (contract §5).

    ``env`` defaults to ``os.environ``; pass a mapping to test without touching
    the process environment. Missing/blank keys fall back to the defaults.
    """
    e = os.environ if env is None else env
    return cls(
      provider=(e.get("STENCIL_LLM_PROVIDER") or "").strip() or "ollama",
      base_url=(e.get("STENCIL_LLM_BASE_URL") or "").strip(),
      model=(e.get("STENCIL_LLM_MODEL") or "").strip(),
      api_key=(e.get("STENCIL_LLM_API_KEY") or "").strip(),
      server_url=(e.get("STENCIL_LLM_SERVER_URL") or "").strip(),
    )
