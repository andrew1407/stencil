"""Shared urllib plumbing — the seam every network test stubs.

Request assembly, the single call site that opens a request (redirects refused, body
capped) and the structured-error parser, plus :class:`ServerError`. Reused verbatim by
``pystencil.llm``'s LlmClient, which passes the longer ``_LLM_TIMEOUT``.
"""

from __future__ import annotations

import importlib.resources
import json
import urllib.error
import urllib.request
from typing import Any

from .._ffi.types import NoneType
from .._net import MAX_FETCH_BYTES, _no_redirect_opener


# Bound every REST call so a hostile/slow/hung server can't block the caller
# indefinitely (seconds).
_REQUEST_TIMEOUT = 30.0

# LLM calls get a much longer bound: a vision op-plan routinely runs a minute or two. The
# figure is timeouts.chatSeconds from the checked-in providers asset (byte-pinned).
_LLM_TIMEOUT = float(
  json.loads(
    importlib.resources.files("pystencil")
    .joinpath("_data/providers.json")
    .read_text(encoding="utf-8")
  )["timeouts"]["chatSeconds"]
)


def _json_request(
  method: str, url: str, body: Any = None, bearer: (str | NoneType) = None
) -> urllib.request.Request:
  """Assemble a Request whose non-None ``body`` is JSON-encoded.

  A non-None ``bearer`` adds ``Authorization: Bearer <bearer>`` (pass "" for an
  empty token — this client always sends the header; the LLM client passes None
  to omit it entirely).
  """
  headers: dict[str, str] = dict()
  data: (bytes | NoneType) = None
  if body is not None:
    headers["Content-Type"] = "application/json"
    data = json.dumps(body).encode("utf-8")
  req = urllib.request.Request(url, data=data, headers=headers, method=method)
  if bearer is not None: _set_bearer(req, bearer)
  return req


def _set_bearer(req: urllib.request.Request, bearer: str) -> None:
  """``Authorization: Bearer <bearer>`` as an unredirected header, which urllib never
  copies onto a redirect's follow-up request."""
  req.add_unredirected_header("Authorization", "Bearer " + bearer)


# Parity with the CLI, whose REST client reads through net.zig's MAX_FETCH_BYTES. An error
# body is a small {code, message}; past its cap it is read as a non-JSON body.
_MAX_RESPONSE_BYTES = MAX_FETCH_BYTES
_MAX_ERROR_BYTES = 64 * 1024


def _http_open(
  req: urllib.request.Request,
  error_from,
  *,
  context=None,
  timeout: float = _REQUEST_TIMEOUT,
) -> tuple:
  """Execute a Request under ``timeout``; returns ``(status, payload bytes)``.

  A non-2xx HTTPError is translated via ``error_from`` (each client's exception
  builder); a network-level ``URLError`` propagates as ``OSError``. The one place
  real network happens for both ServerConnection and LlmClient — the latter passes
  the longer ``_LLM_TIMEOUT``.

  A 30x is refused, never followed, so the bearer never reaches the host it names; the
  TLS ``context`` rides that same opener. A body past ``_MAX_RESPONSE_BYTES`` is an
  ``OSError``.
  """
  try:
    resp = _no_redirect_opener(context).open(req, timeout=timeout)
  except urllib.error.HTTPError as e:
    raise error_from(e) from None
  with resp:
    payload = resp.read(_MAX_RESPONSE_BYTES + 1)
    status = getattr(resp, "status", resp.getcode())
  if len(payload) > _MAX_RESPONSE_BYTES:
    raise OSError("response from %s exceeds the %d-byte cap"
                  % (req.full_url, _MAX_RESPONSE_BYTES))
  return status, payload


def _parse_http_error(e: urllib.error.HTTPError) -> tuple[str, str]:
  """Parse an HTTPError body's structured ``{code, message}`` when present.

  Returns ``(code, message)``; a missing/non-JSON body keeps ``code`` empty and
  the generic ``"HTTP <status>"`` message. Shared by both clients' error
  builders (the server's protocol.ErrorResponse and the LLM proxy's errors use
  the same shape).
  """
  code = ""
  message = f"HTTP {e.code}"
  if 300 <= e.code < 400 and e.msg: message = "%s (HTTP %d)" % (e.msg, e.code)
  try:
    body = e.read(_MAX_ERROR_BYTES)
    if body:
      parsed = json.loads(body.decode("utf-8"))
      if isinstance(parsed, dict):
        code = parsed.get("code", "") or ""
        message = parsed.get("message", message) or message
  except Exception:
    # Non-JSON error body — keep the generic "HTTP <status>" message.
    pass
  return code, message


class ServerError(Exception):
  """A non-2xx REST response.

  Carries the server's structured {code, message} (protocol.ErrorResponse)
  when present, plus the raw HTTP status. `code` mirrors protocol's error
  codes (e.g. "conflict", "notFound", "unauthorized").
  """

  def __init__(self, code: str, message: str, status: (int | NoneType) = None) -> None:
    super().__init__(f"{code}: {message}" if code else message)
    self.code = code
    self.message = message
    self.status = status

