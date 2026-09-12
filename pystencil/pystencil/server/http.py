from __future__ import annotations

"""Shared urllib plumbing — the seam every network test stubs.

Request assembly, the redirect-refusing opener, the single ``urlopen`` call site and
the structured-error parser, plus :class:`ServerError`. Reused verbatim by
``pystencil.llm``'s LlmClient, which passes the longer ``_LLM_TIMEOUT``.
"""

import importlib.resources
import json
import urllib.error
import urllib.request
from typing import Any

from .._types import NoneType


# Bound every REST call so a hostile/slow/hung server can't block the caller
# indefinitely (seconds).
_REQUEST_TIMEOUT = 30.0

# LLM calls get their own, much longer bound: a vision op-plan routinely runs a
# minute or two, so the REST figure would time out mid-answer. The figure is the
# canonical timeouts.chatSeconds from the checked-in providers asset (byte-pinned
# by tests/test_canonical_drift.py) — the same 120 s every other client allows.
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
  if bearer is not None:
    headers["Authorization"] = "Bearer " + bearer
  data: (bytes | NoneType) = None
  if body is not None:
    headers["Content-Type"] = "application/json"
    data = json.dumps(body).encode("utf-8")
  return urllib.request.Request(url, data=data, headers=headers, method=method)


class _NoRedirect(urllib.request.HTTPRedirectHandler):
  """A redirect handler that refuses instead of following.

  urllib re-sends the original headers — ``Authorization`` included — to the redirect
  target, which would hand the key to a second host. The 30x surfaces as an ``HTTPError``.
  """

  def redirect_request(self, req, fp, code, msg, headers, newurl):  # noqa: D102
    return None


_no_redirect_opener = urllib.request.build_opener(_NoRedirect)


def _http_open(
  req: urllib.request.Request,
  error_from,
  *,
  context=None,
  follow_redirects: bool = True,
  timeout: float = _REQUEST_TIMEOUT,
) -> tuple:
  """Execute a Request under ``timeout``; returns ``(status, payload bytes)``.

  A non-2xx HTTPError is translated via ``error_from`` (each client's exception
  builder); a network-level ``URLError`` propagates as ``OSError``. The one place
  real network happens for both ServerConnection and LlmClient — the latter passes
  the longer ``_LLM_TIMEOUT``.

  ``follow_redirects=False`` refuses 30x responses rather than replaying the
  request — and its credential headers — against whatever host was named.
  """
  try:
    if follow_redirects:
      resp = urllib.request.urlopen(req, context=context, timeout=timeout)
    else:
      resp = _no_redirect_opener.open(req, timeout=timeout)
  except urllib.error.HTTPError as e:
    raise error_from(e) from None
  with resp:
    payload = resp.read()
    status = getattr(resp, "status", resp.getcode())
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
  try:
    body = e.read()
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

