"""``NoneType`` for the ``(X | NoneType)`` union spelling; ``types`` grew it in 3.10."""

from __future__ import annotations

try:
  from types import NoneType
except ImportError:
  NoneType = type(None)
