"""The provider client (contract §6): one non-streaming chat call per provider, over
:mod:`pystencil.server`'s urllib plumbing; the wire shapes are :mod:`.wire`'s table.
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
  """A per-provider LLM chat client. Messages are ``{"role", "text", "images"}`` dicts, each
  image a ``(media_type, bytes)`` tuple. ``stencil-server`` takes a ``server`` connection or
  the config's ``server_url`` + ``token``; ``anthropic`` uses the config's session key.
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
    """The one network seam: non-2xx or a bad payload is :class:`LlmError`, a ``URLError``
    propagates as ``OSError``. Redirects are refused: urllib would replay the key to the 30x."""
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
    """The reply text (§6); a ``max_tokens``/``refusal`` stop raises :class:`LlmError`."""
    text = self._wire.reply(payload)
    if not isinstance(text, str):
      raise LlmError("malformed %s response: no reply text" % self.config.provider)
    return text

  def chat(self, messages: Messages, system: str = LLM_SYSTEM_PROMPT) -> str:
    """Send one non-streaming chat call and return the raw reply text."""
    return self._extract_reply(self._open(self._build_request(messages, system)))
