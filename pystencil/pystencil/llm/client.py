from __future__ import annotations

"""The provider client (contract §6): one non-streaming chat call per provider, over
the same shared urllib plumbing as :mod:`pystencil.server`.
"""

import base64
import json
import urllib.error
from typing import Any, Sequence

from .._types import NoneType
from ..server import _http_open, _json_request, _parse_http_error, _LLM_TIMEOUT
from .config import ACCEPTED_MEDIA_TYPES, DEFAULT_BASE_URLS, LlmConfig
from .errors import LlmError, _clean_detail
from .prompt import LLM_SYSTEM_PROMPT

Messages = Sequence[dict]


# ── provider client (contract §6) ─────────────────────────────────────────────
def _b64(data: Any) -> str:
  """Base64-encode image bytes (an already-encoded str passes through)."""
  if isinstance(data, str):
    return data
  return base64.b64encode(bytes(data)).decode("ascii")


def _msg_parts(m: dict) -> tuple[str, str, list]:
  """Unpack an internal message dict into (role, text, images)."""
  return (
    m.get("role") or "user",
    m.get("text") or "",
    list(m.get("images") or []),
  )


class LlmClient:
  """A per-provider LLM chat client (urllib, no deps, offline-testable).

  Messages are dicts ``{"role": "user"|"assistant", "text": str, "images": [...]}``
  where each image is a ``(media_type, bytes)`` tuple, base64-encoded into the
  provider's wire shape (contract §6). Request builders are pure (no network);
  every call executes through the private :meth:`_open` seam, mirroring
  :class:`pystencil.server.ServerConnection`. For ``stencil-server``, pass either
  a ``server`` object (a :class:`~pystencil.server.ServerConnection`, whose
  ``.base`` URL and ``.token`` are read) or a config ``server_url`` + ``token``.
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

  # ── request plumbing ──
  def _build_request(
    self, messages: Messages, system: str = LLM_SYSTEM_PROMPT
  ) -> urllib.request.Request:
    """Pure builder: assemble the provider-specific POST (no network)."""
    provider = self.config.provider
    if provider == "ollama":
      return self._ollama_request(messages, system)
    if provider == "openai-compat":
      return self._openai_request(messages, system)
    return self._server_request(messages, system)

  def _base_post(
    self, path: str, wire: list, bearer: (str | NoneType) = None
  ) -> urllib.request.Request:
    """The §6.1/§6.2 shared envelope: ``{model, stream:false, messages}`` POSTed
    to ``{baseUrl}{path}``."""
    body = {"model": self.config.model, "stream": False, "messages": wire}
    return self._post(self.config.base_url.rstrip("/") + path, body, bearer=bearer)

  def _ollama_request(
    self, messages: Messages, system: str
  ) -> urllib.request.Request:
    """``POST {baseUrl}/api/chat`` — native chat, images as bare base64 (§6.1)."""
    wire: list = [{"role": "system", "content": system}]
    for m in messages:
      role, text, images = _msg_parts(m)
      entry: dict = {"role": role, "content": text}
      if images:
        entry["images"] = [_b64(data) for _mt, data in images]
      wire.append(entry)
    return self._base_post("/api/chat", wire)

  def _openai_request(
    self, messages: Messages, system: str
  ) -> urllib.request.Request:
    """``POST {baseUrl}/chat/completions`` — images as data URLs; optional
    ``Authorization: Bearer <apiKey>`` (§6.2)."""
    wire: list = [{"role": "system", "content": system}]
    for m in messages:
      role, text, images = _msg_parts(m)
      if images:
        content: Any = [{"type": "text", "text": text}]
        for mt, data in images:
          content.append(
            {
              "type": "image_url",
              "image_url": {"url": "data:%s;base64,%s" % (mt, _b64(data))},
            }
          )
      else:
        content = text
      wire.append({"role": role, "content": content})
    return self._base_post(
      "/chat/completions", wire, bearer=self.config.api_key or None
    )

  def _server_request(
    self, messages: Messages, system: str
  ) -> urllib.request.Request:
    """``POST {serverUrl}/llm/chat`` — the collaboration server's Anthropic proxy,
    authenticated with the existing Stencil session token (§6.3)."""
    if not self._server_url:
      raise LlmError(
        "no stencil-server URL configured — set STENCIL_LLM_SERVER_URL or "
        "pass a ServerConnection"
      )
    wire: list = list()
    for m in messages:
      role, text, images = _msg_parts(m)
      entry: dict = {"role": role, "text": text}
      if images:
        entry["images"] = [
          {"mediaType": mt, "data": _b64(data)} for mt, data in images
        ]
      wire.append(entry)
    body: dict = {"system": system, "messages": wire}
    if self.config.model:
      body["model"] = self.config.model
    return self._post(self._server_url + "/llm/chat", body, bearer=self._token or "")

  @staticmethod
  def _post(url: str, body: dict, bearer: (str | NoneType) = None) -> urllib.request.Request:
    """A JSON POST Request; ``bearer`` adds ``Authorization`` (None omits it).
    Delegates to the request builder shared with :mod:`pystencil.server`."""
    return _json_request("POST", url, body, bearer=bearer)

  def _open(self, req: urllib.request.Request) -> Any:
    """Execute a Request, translating non-2xx / bad payloads into :class:`LlmError`
    (a network-level ``URLError`` propagates as ``OSError``, like the server client).

    The single network seam (tests monkey-patch this, like ServerConnection._open);
    the urlopen/HTTPError plumbing is the one shared with the server client, under the
    LLM timeout rather than the REST one.
    Redirects are refused: urllib would replay the key against the host the 30x named.
    """
    _status, payload = _http_open(
      req, self._error_from, follow_redirects=False, timeout=_LLM_TIMEOUT)
    if not payload:
      raise LlmError("empty response from the LLM provider")
    try:
      return json.loads(payload.decode("utf-8"))
    except (ValueError, UnicodeDecodeError):
      raise LlmError("non-JSON response from the LLM provider") from None

  @staticmethod
  def _error_from(e: urllib.error.HTTPError) -> LlmError:
    """Build an LlmError from an HTTPError, parsing a ``{code, message}`` body
    when present (e.g. the server's 503 ``llmDisabled``).

    The message alone is the text: it already says the reason once (§6.3), and the
    machine ``code`` in front of it only restated it — it stays on the exception,
    where a caller can branch on it.
    """
    code, message = _parse_http_error(e)
    return LlmError(_clean_detail(message), code=code, status=e.code)

  def _extract_reply(self, payload: Any) -> str:
    """Pull the reply text out of a provider response (contract §6).

    For ``stencil-server``, a ``stopReason`` of ``max_tokens``/``refusal`` raises
    a typed :class:`LlmError` — those replies are never parsed as plans.
    """
    provider = self.config.provider
    text: Any = None
    if provider == "ollama":
      msg = payload.get("message") if isinstance(payload, dict) else None
      text = msg.get("content") if isinstance(msg, dict) else None
    elif provider == "openai-compat":
      choices = payload.get("choices") if isinstance(payload, dict) else None
      first = choices[0] if isinstance(choices, list) and choices else None
      msg = first.get("message") if isinstance(first, dict) else None
      text = msg.get("content") if isinstance(msg, dict) else None
    else:  # stencil-server
      if not isinstance(payload, dict):
        payload = dict()
      stop = payload.get("stopReason") or ""
      text = payload.get("text")
      if stop == "max_tokens":
        raise LlmError(
          "response truncated (max_tokens) — not parsed as a plan",
          stop_reason="max_tokens",
        )
      if stop == "refusal":
        raise LlmError(
          "the model refused to answer", stop_reason="refusal"
        )
    if not isinstance(text, str):
      raise LlmError("malformed %s response: no reply text" % provider)
    return text

  # ── the one public call ──
  def chat(self, messages: Messages, system: str = LLM_SYSTEM_PROMPT) -> str:
    """Send one non-streaming chat call and return the raw reply text."""
    return self._extract_reply(self._open(self._build_request(messages, system)))
