"""The provider client (contract §6): one non-streaming chat call per provider, over
the same shared urllib plumbing as :mod:`pystencil.server`. The wire shapes it speaks
are :mod:`.wire`'s table.
"""

from __future__ import annotations

import json
import urllib.error
from typing import Any

from .._ffi.types import NoneType
from ..server import _http_open, _json_request, _LLM_TIMEOUT
from .config import CHAT_PATHS, WIRE_OF, LlmConfig
from .errors import LlmError
from .prompt import LLM_SYSTEM_PROMPT
from .wire import WIRES, Messages, _proxy_error


class LlmClient:
  """A per-provider LLM chat client (urllib, no deps, offline-testable).

  Messages are dicts ``{"role": "user"|"assistant", "text": str, "images": [...]}``
  where each image is a ``(media_type, bytes)`` tuple, base64-encoded into the
  provider's wire shape (contract §6). Request builders are pure (no network);
  every call executes through the private :meth:`_open` seam, mirroring
  :class:`pystencil.server.ServerConnection`. For ``stencil-server``, pass either
  a ``server`` object (a :class:`~pystencil.server.ServerConnection`, whose
  ``.base`` URL and ``.token`` are read) or a config ``server_url`` + ``token``.
  ``anthropic`` goes straight to the Messages API with the config's session key.
  """

  def __init__(
    self,
    config: (LlmConfig | NoneType) = None,
    *,
    server: Any = None,
    token: str = "",
  ) -> None:
    self.config = config if config is not None else LlmConfig.from_env()
    if server is not None:
      url = getattr(server, "base", "") or getattr(server, "base_url", "") or ""
      self._server_url = url.rstrip("/")
      self._token = token or getattr(server, "token", "") or ""
    else:
      self._server_url = (self.config.server_url or "").rstrip("/")
      self._token = token

  @property
  def _wire(self):
    return WIRES[WIRE_OF[self.config.provider]]

  # ── request plumbing ──
  def _build_request(
    self, messages: Messages, system: str = LLM_SYSTEM_PROMPT
  ) -> urllib.request.Request:
    """Pure builder: the provider's wire body POSTed to ``{base}{chatPath}`` (no network).
    A wire whose credential is missing raises here, so nothing is ever sent."""
    provider = self.config.provider
    base, bearer, headers = self._wire.endpoint(self.config, self._server_url, self._token)
    body = self._wire.body(messages, system, self.config.model)
    req = _json_request("POST", base + CHAT_PATHS[provider], body, bearer=bearer)
    # Unredirected, as the bearer is: urllib never copies these onto a 30x's follow-up.
    for name, value in headers.items(): req.add_unredirected_header(name, value)
    return req

  def _open(self, req: urllib.request.Request) -> Any:
    """Execute a Request, translating non-2xx / bad payloads into :class:`LlmError`
    (a network-level ``URLError`` propagates as ``OSError``, like the server client).

    The single network seam (tests monkey-patch this, like ServerConnection._open);
    the urlopen/HTTPError plumbing is the one shared with the server client, under the
    LLM timeout rather than the REST one.
    Redirects are refused: urllib would replay the key against the host the 30x named.
    """
    _status, payload = _http_open(req, self._wire_error, timeout=_LLM_TIMEOUT)
    if not payload: raise LlmError("empty response from the LLM provider")
    try:
      return json.loads(payload.decode("utf-8"))
    except (ValueError, UnicodeDecodeError):
      raise LlmError("non-JSON response from the LLM provider") from None

  def _wire_error(self, e: urllib.error.HTTPError) -> LlmError:
    """This provider's typed error for a non-2xx answer (§6.3, §6.5)."""
    return self._wire.error(e, self.config)

  @staticmethod
  def _error_from(e: urllib.error.HTTPError) -> LlmError:
    """The typed error a ``{code, message}`` body makes (e.g. the server's 503
    ``llmDisabled``); the code stays on the exception, where a caller can branch on it."""
    return _proxy_error(e, None)

  def _extract_reply(self, payload: Any) -> str:
    """Pull the reply text out of a provider response (contract §6).

    For ``stencil-server`` and ``anthropic``, a stop reason of ``max_tokens``/``refusal``
    raises a typed :class:`LlmError` — those replies are never parsed as plans.
    """
    text = self._wire.reply(payload)
    if not isinstance(text, str):
      raise LlmError("malformed %s response: no reply text" % self.config.provider)
    return text

  # ── the one public call ──
  def chat(self, messages: Messages, system: str = LLM_SYSTEM_PROMPT) -> str:
    """Send one non-streaming chat call and return the raw reply text."""
    return self._extract_reply(self._open(self._build_request(messages, system)))
