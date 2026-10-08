"""Provider configuration (contract §5): the provider list, their default base URLs,
the attachment caps, the direct ``anthropic`` wire's constants, and :class:`LlmConfig` —
env-loaded or passed in, never discovered.
"""

from __future__ import annotations

import importlib.resources
import json
import os
import time
from dataclasses import dataclass, field
from typing import Any, Callable

from .._ffi.types import NoneType

MAX_ATTACHMENTS = 3

# The console's §2.1 upload set: how many /upload-ed images one turn may index with an
# `image` op (the cli console's max_attachments). Past it the oldest upload falls off.
MAX_UPLOAD_ATTACHMENTS = 8

# §7.
ACCEPTED_MEDIA_TYPES = ("image/png", "image/jpeg", "image/webp", "image/gif")

# §5, from the build-time copy of providers.json; stencil-server's null default drops out.
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
WIRE_OF = {name: p["wire"] for name, p in _PROVIDERS_ASSET["providers"].items()}
CHAT_PATHS = {name: p["chatPath"] for name, p in _PROVIDERS_ASSET["providers"].items()}

# §6.5: what a direct anthropic turn sends, and how long its session key lives (seconds).
ANTHROPIC_VERSION = _PROVIDERS_ASSET["anthropicUpstream"]["version"]
DEFAULT_MODEL = _PROVIDERS_ASSET["serverDefaults"]["model"]
MAX_TOKENS = _PROVIDERS_ASSET["serverDefaults"]["maxTokens"]
KEY_TTL_SECONDS = 60 * _PROVIDERS_ASSET["providers"]["anthropic"]["sessionKey"]["ttlMinutes"]


@dataclass(repr=False)
class LlmConfig:
  """Which LLM endpoint to talk to (§5). ``server_url`` is ``stencil-server``'s alone; an
  empty ``base_url`` takes the provider default. The key lives here alone, redacted in the
  repr; on ``anthropic`` :meth:`session_key` drops it ``KEY_TTL_SECONDS`` after it was set.
  """

  provider: str = "ollama"
  base_url: str = ""
  model: str = ""
  api_key: str = ""
  server_url: str = ""
  clock: Callable[[], float] = field(default=time.time, compare=False)

  def __post_init__(self) -> None:
    self.provider = (self.provider or "ollama").strip().lower()
    if self.provider not in PROVIDERS:
      raise ValueError(
        "unknown LLM provider %r — use one of: %s"
        % (self.provider, ", ".join(PROVIDERS))
      )
    if not self.base_url: self.base_url = DEFAULT_BASE_URLS.get(self.provider, "")
    self._url_pinned = False
    self._key_at = self.clock()

  def __setattr__(self, name: str, value: Any) -> None:
    super().__setattr__(name, value)
    # Every assignment of the key restarts its TTL; __post_init__ stamps the first.
    if name == "api_key" and "_key_at" in self.__dict__:
      super().__setattr__("_key_at", self.clock())

  def __repr__(self) -> str:
    return "LlmConfig(provider=%r, base_url=%r, model=%r, api_key=%s, server_url=%r)" % (
      self.provider, self.base_url, self.model,
      "'<redacted>'" if self.api_key else "''", self.server_url,
    )

  def key_expires_in(self) -> float:
    """Seconds the key has left before :meth:`session_key` drops it (≤ 0: expired)."""
    return self._key_at + KEY_TTL_SECONDS - self.clock()

  def session_key(self) -> str:
    """The key a direct ``anthropic`` turn sends, dropped first once its TTL passed."""
    if self.api_key and self.key_expires_in() <= 0: self.api_key = ""
    return self.api_key

  def set_base_url(self, url: str) -> "LlmConfig":
    """Pin a base URL that :meth:`set_provider` then keeps across switches."""
    self.base_url = url
    self._url_pinned = True
    return self

  def set_provider(
    self, provider: str, *, keep_url: (bool | NoneType) = None
  ) -> "LlmConfig":
    """Switch providers in place, re-filling the default ``base_url`` unless ``keep_url``
    (default: whether it was pinned). An unknown provider raises ``ValueError``, changing nothing."""
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
    """A config from the ``STENCIL_LLM_*`` keys of ``env`` (default ``os.environ``)."""
    e = os.environ if env is None else env
    return cls(
      provider=(e.get("STENCIL_LLM_PROVIDER") or "").strip() or "ollama",
      base_url=(e.get("STENCIL_LLM_BASE_URL") or "").strip(),
      model=(e.get("STENCIL_LLM_MODEL") or "").strip(),
      api_key=(e.get("STENCIL_LLM_API_KEY") or "").strip(),
      server_url=(e.get("STENCIL_LLM_SERVER_URL") or "").strip(),
    )
