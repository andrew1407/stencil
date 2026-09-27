"""Each provider's wire shape (contract §6): the request body, the reply extractor, the
endpoint with its credentials, and the error builder, keyed by ``providers.json``'s ``wire``.

Pure — nothing here opens a connection; :class:`~pystencil.llm.LlmClient` does.
"""

from __future__ import annotations

import base64
import json
import urllib.error
import urllib.parse
from typing import Any, Callable, NamedTuple, Sequence

from ..server import _parse_http_error, is_loopback_host
from ..server.http import _MAX_ERROR_BYTES
from .config import ANTHROPIC_VERSION, DEFAULT_MODEL, MAX_TOKENS, LlmConfig
from .errors import LlmError, _clean_detail, stop_error, upstream_message

Messages = Sequence[dict]

#: §6.5: a direct anthropic turn with no session key sends nothing and fails with this.
NO_SESSION_KEY = "no API key for this session"


def _b64(data: Any) -> str:
  """Base64-encode image bytes (an already-encoded str passes through)."""
  if isinstance(data, str): return data
  return base64.b64encode(bytes(data)).decode("ascii")


def _msg_parts(m: dict) -> tuple[str, str, list]:
  """Unpack an internal message dict into (role, text, images)."""
  return (
    m.get("role") or "user",
    m.get("text") or "",
    list(m.get("images") or []),
  )


def _ollama_body(messages: Messages, system: str, model: str) -> dict:
  """Native chat, images as bare base64 (§6.1)."""
  wire: list = [{"role": "system", "content": system}]
  for m in messages:
    role, text, images = _msg_parts(m)
    entry: dict = {"role": role, "content": text}
    if images: entry["images"] = [_b64(data) for _mt, data in images]
    wire.append(entry)
  return {"model": model, "stream": False, "messages": wire}


def _openai_body(messages: Messages, system: str, model: str) -> dict:
  """Chat completions, images as data URLs (§6.2)."""
  wire: list = [{"role": "system", "content": system}]
  for m in messages:
    role, text, images = _msg_parts(m)
    content: Any = text
    if images:
      content = [{"type": "text", "text": text}] + [
        {"type": "image_url", "image_url": {"url": "data:%s;base64,%s" % (mt, _b64(data))}}
        for mt, data in images
      ]
    wire.append({"role": role, "content": content})
  return {"model": model, "stream": False, "messages": wire}


def _server_body(messages: Messages, system: str, model: str) -> dict:
  """The collaboration server's Anthropic proxy: ``{system, messages, model?}`` (§6.3)."""
  wire: list = list()
  for m in messages:
    role, text, images = _msg_parts(m)
    entry: dict = {"role": role, "text": text}
    if images:
      entry["images"] = [{"mediaType": mt, "data": _b64(data)} for mt, data in images]
    wire.append(entry)
  body: dict = {"system": system, "messages": wire}
  if model: body["model"] = model
  return body


def _anthropic_body(messages: Messages, system: str, model: str) -> dict:
  """The server's own upstream body (§6.5): a turn's text block first, then its images."""
  wire: list = list()
  for m in messages:
    role, text, images = _msg_parts(m)
    content: list = [{"type": "text", "text": text}] if text else []
    content += [
      {"type": "image", "source": {"type": "base64", "media_type": mt, "data": _b64(data)}}
      for mt, data in images
    ]
    wire.append({"role": role, "content": content})
  body: dict = {"model": model or DEFAULT_MODEL, "max_tokens": MAX_TOKENS}
  if system: body["system"] = system
  body["messages"] = wire
  return body


def _field(value: Any, key: str) -> Any:
  return value.get(key) if isinstance(value, dict) else None


def _ollama_reply(payload: Any) -> Any:
  return _field(_field(payload, "message"), "content")


def _openai_reply(payload: Any) -> Any:
  choices = _field(payload, "choices")
  first = choices[0] if isinstance(choices, list) and choices else None
  return _field(_field(first, "message"), "content")


def _server_reply(payload: Any) -> Any:
  """``text``; a ``stopReason`` of ``max_tokens``/``refusal`` raises instead (§6.3)."""
  text = _field(payload, "text")
  err = stop_error(_field(payload, "stopReason") or "", text)
  if err is not None: raise err
  return text


def _anthropic_reply(payload: Any) -> Any:
  """Every ``text`` block of ``content[]`` joined; ``stop_reason`` maps as §6.3's does."""
  blocks = _field(payload, "content")
  text = None
  if isinstance(blocks, list):
    text = "".join(
      b["text"] for b in blocks
      if _field(b, "type") == "text" and isinstance(b.get("text"), str)
    )
  err = stop_error(_field(payload, "stop_reason") or "", text)
  if err is not None: raise err
  return text


def _ollama_endpoint(config: LlmConfig, _server_url: str, _token: str) -> tuple:
  return config.base_url.rstrip("/"), None, {}


def _openai_endpoint(config: LlmConfig, _server_url: str, _token: str) -> tuple:
  """The optional API key rides as the bearer."""
  return config.base_url.rstrip("/"), (config.api_key or None), {}


def _server_endpoint(_config: LlmConfig, server_url: str, token: str) -> tuple:
  """The collaboration server's URL, with the session token as the bearer."""
  if not server_url:
    raise LlmError(
      "no stencil-server URL configured — set STENCIL_LLM_SERVER_URL or pass a ServerConnection"
    )
  return server_url, token or "", {}


def _refuse_plain_http(url: str) -> None:
  """§6.5: the key rides plain http only to a loopback host (a local mock or proxy the user
  named); an unparseable authority counts as no host, so it is refused too."""
  if url[:7].lower() != "http://": return
  try:
    host = urllib.parse.urlsplit(url).hostname or ""
  except ValueError:
    host = ""
  if is_loopback_host(host): return
  raise LlmError(
    "refusing to send the API key to '%s' over plain http — use https" % host, code="llmDisabled")


def _anthropic_endpoint(config: LlmConfig, _server_url: str, _token: str) -> tuple:
  """§6.5 headers, never ``Authorization``; no live session key, or a key bound for plain
  http off loopback, refuses before any send."""
  key = config.session_key()
  if not key: raise LlmError(NO_SESSION_KEY, code="llmDisabled")
  base = config.base_url.rstrip("/")
  _refuse_plain_http(base)
  headers = {"x-api-key": key, "anthropic-version": ANTHROPIC_VERSION}
  return base, None, headers


def _proxy_error(e: urllib.error.HTTPError, _config: LlmConfig) -> LlmError:
  """A ``{code, message}`` body when present (e.g. the server's 503 ``llmDisabled``); the
  message alone is the text, since it already says the reason once (§6.3)."""
  code, message = _parse_http_error(e)
  return LlmError(_clean_detail(message), code=code, status=e.code)


def _anthropic_error(e: urllib.error.HTTPError, config: LlmConfig) -> LlmError:
  """The ``{"type":"error","error":{type,message}}`` envelope and the status, classified
  by the server's rules, with the session key's fragment veto (§6.5)."""
  err_type, detail = "", ""
  try:
    envelope = _field(json.loads(e.read(_MAX_ERROR_BYTES).decode("utf-8")), "error")
  except (ValueError, UnicodeDecodeError, OSError):
    envelope = None
  message = _field(envelope, "message")
  if isinstance(message, str) and message:
    err_type, detail = str(_field(envelope, "type") or ""), message
  return LlmError(upstream_message(e.code, err_type, detail, config.api_key), status=e.code)


class _Wire(NamedTuple):
  body: Callable[[Messages, str, str], dict]
  reply: Callable[[Any], Any]
  endpoint: Callable[[LlmConfig, str, str], tuple]
  error: Callable[[urllib.error.HTTPError, LlmConfig], LlmError]


# Strategy table keyed by providers.json's ``wire``; routes come from its ``chatPath``.
WIRES = {
  "ollama": _Wire(_ollama_body, _ollama_reply, _ollama_endpoint, _proxy_error),
  "openai": _Wire(_openai_body, _openai_reply, _openai_endpoint, _proxy_error),
  "server": _Wire(_server_body, _server_reply, _server_endpoint, _proxy_error),
  "anthropic": _Wire(_anthropic_body, _anthropic_reply, _anthropic_endpoint, _anthropic_error),
}
