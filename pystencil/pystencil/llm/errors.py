"""The LLM exception family, the provider-detail scrubber and the upstream classifier a direct
``anthropic`` failure is worded by (twin of the server's ``internal/llm/upstream.go`` +
``sanitize.go``).
"""

from __future__ import annotations

import re
from typing import Any

from .._ffi.types import NoneType

#: How much of a provider's own prose an error may quote (contract §6.3).
MAX_PROVIDER_DETAIL = 200

#: The shortest run of a key that still counts as leaking it (the server's veto).
_SECRET_FRAGMENT = 8

_CONTROLISH = re.compile(r"[\x00-\x1f\x7f-\x9f]")
_URLISH = re.compile(r"[a-z][a-z0-9+.-]*://\S+", re.I)
_SECRETISH = re.compile(
  r"(?:bearer|basic) +[A-Za-z0-9._~+/=-]{8,}"
  r"|\b(?:sk|pk|api[-_]?key|key|token|secret)[-_=:][A-Za-z0-9._-]{6,}"
  r"|[A-Za-z0-9_-]{24,}",
  re.I,
)


def _clean_detail(text: str, secret: str = "") -> str:
  """Provider prose made printable: controls out, URLs and token-shaped runs redacted,
  truncated; "" when any 8-character run of ``secret`` survives (``sanitizeUpstreamText``)."""
  t = _CONTROLISH.sub(" ", text[: 4 * MAX_PROVIDER_DETAIL])
  t = _SECRETISH.sub("[redacted]", _URLISH.sub("[redacted]", t))
  t = " ".join(t.split())
  t = t if len(t) <= MAX_PROVIDER_DETAIL else t[: MAX_PROVIDER_DETAIL - 1].strip() + "…"
  n = _SECRET_FRAGMENT
  if len(secret) >= n and any(secret[i:i + n] in t for i in range(len(secret) - n + 1)):
    return ""
  return t


class LlmError(Exception):
  """A transport/provider failure: non-2xx, a malformed payload, or a §6.3
  ``max_tokens``/``refusal`` stop, never parsed as a plan."""

  def __init__(
    self,
    message: str,
    *,
    code: str = "",
    status: (int | NoneType) = None,
    stop_reason: (str | NoneType) = None,
  ) -> None:
    super().__init__(message)
    self.message = message
    self.code = code
    self.status = status
    self.stop_reason = stop_reason


class LlmPlanError(LlmError):
  """An op-plan object failed strict validation (§1/§2); nothing executes."""


class LlmExecutionError(LlmError):
  """A validated plan this surface cannot execute (the ``frame`` op needs video)."""


def stop_error(stop: str, text: Any) -> (LlmError | NoneType):
  """The §6.3 stop reasons that are never a reply: ``max_tokens`` truncates, and
  ``refusal`` refuses in the model's own words when it gave any."""
  if stop == "max_tokens":
    return LlmError(
      "response truncated (max_tokens) — not parsed as a plan", stop_reason="max_tokens")
  if stop == "refusal":
    said = text.strip() if isinstance(text, str) else ""
    return LlmError(said or "the model refused to answer", stop_reason="refusal")
  return None


# (reason, error-type substrings, message substrings, statuses): the first match wins, in
# upstream.go classifyUpstream's order, and a 5xx nothing claimed is the last reason.
_UPSTREAM_KINDS = (
  ("the LLM provider is out of credits or has no active billing",
   ("insufficient_quota", "billing", "credit"),
   ("credit balance", "insufficient_quota", "insufficient quota", "purchase credits", "billing",
    "out of credits"), (402,)),
  ("the LLM provider rejected the API key",
   ("authentication", "invalid_api_key", "permission", "unauthorized", "forbidden"), (), (401, 403)),
  ("the LLM provider does not have the requested model", ("model_not_found", "not_found"),
   ("model not found", "unknown model", "does not exist", "try pulling", "no such model"), (404,)),
  ("the LLM provider is rate-limiting this key", ("rate_limit",), (), (429,)),
  ("the LLM provider did not respond in time", (), (), (408, 504)),
  ("the LLM provider is temporarily unavailable", ("overloaded", "api_error"), (), ()),
)


def upstream_message(status: int, err_type: str, detail: str, secret: str) -> str:
  """§6.5: a recognised condition is its reason alone; anything else is the status plus
  the sanitized upstream text, dropped whole when it shows a fragment of ``secret``."""
  t, m = err_type.lower(), detail.lower()
  for reason, types, texts, statuses in _UPSTREAM_KINDS:
    if any(s in t for s in types) or any(s in m for s in texts) or status in statuses:
      return reason
  if status >= 500: return _UPSTREAM_KINDS[-1][0]
  head = "the LLM provider returned an error (HTTP %d)" % status
  clean = _clean_detail(detail, secret)
  return "%s: %s" % (head, clean) if clean else head
