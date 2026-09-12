"""``NoneType`` for the ``(X | NoneType)`` union spelling; ``types`` grew it in 3.10."""

try:
  from types import NoneType
except ImportError:
  NoneType = type(None)
