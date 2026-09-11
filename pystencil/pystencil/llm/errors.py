from __future__ import annotations

"""The LLM exception family and the provider-detail scrubber every message runs through.

A provider's own prose may be echoed (contract §6.3), but only after control
characters, URLs and secret-shaped runs are stripped and it is length-capped.
"""

import re
from typing import Optional

# ── errors ────────────────────────────────────────────────────────────────────
#: How much of a provider's own prose an error may quote (contract §6.3).
MAX_PROVIDER_DETAIL = 200

_CONTROLISH = re.compile(r"[\x00-\x1f\x7f]")
_URLISH = re.compile(r"[a-z][a-z0-9+.-]*://\S+", re.I)
_SECRETISH = re.compile(
    r"(?:bearer|basic) +[A-Za-z0-9._~+/=-]{8,}"
    r"|\b(?:sk|pk|api[-_]?key|key|token|secret)[-_=:][A-Za-z0-9._-]{6,}"
    r"|[A-Za-z0-9_-]{24,}",
    re.I,
)


def _clean_detail(text: str) -> str:
    """Untrusted provider prose made safe to print: control characters out, URLs and
    token-shaped runs redacted (an endpoint may echo the key back), whitespace
    collapsed, hard-truncated. Port of the server's ``sanitizeUpstreamText``."""
    t = _CONTROLISH.sub(" ", text[: 4 * MAX_PROVIDER_DETAIL])
    t = _SECRETISH.sub("[redacted]", _URLISH.sub("[redacted]", t))
    t = " ".join(t.split())
    return t if len(t) <= MAX_PROVIDER_DETAIL else t[: MAX_PROVIDER_DETAIL - 1].strip() + "…"


class LlmError(Exception):
    """An LLM transport/provider failure (non-2xx, malformed payload, or a
    stencil-server ``stopReason`` of ``max_tokens``/``refusal`` — which per contract
    §6.3 must surface as an error, never be parsed as a plan)."""

    def __init__(
        self,
        message: str,
        *,
        code: str = "",
        status: Optional[int] = None,
        stop_reason: Optional[str] = None,
    ) -> None:
        super().__init__(message)
        self.message = message
        self.code = code
        self.status = status
        self.stop_reason = stop_reason


class LlmPlanError(LlmError):
    """A found op-plan JSON object failed strict validation (contract §1/§2): a
    missing/empty reply, a known op with invalid params, or an exceeded limit.
    Nothing executes when this is raised."""


class LlmExecutionError(LlmError):
    """A validated plan could not be executed on this surface (e.g. the ``frame``
    op, which needs a video input pystencil cannot decode)."""
