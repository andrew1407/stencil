"""Tolerant coercion primitives for :mod:`pystencil.layout`'s parsing.

A wrong or missing type falls back to the documented default instead of raising,
matching the lenient JS/CLI layout parsers.
"""

from __future__ import annotations

from typing import Any, Optional


def _as_float(v: Any, default: float) -> float:
    """Coerce to float, falling back to ``default`` on None/bad values."""
    if v is None:
        return default
    try:
        return float(v)
    except (TypeError, ValueError):
        return default


def _as_int(v: Any, default: int) -> int:
    """Coerce to int, falling back to ``default`` on None/bad values."""
    if v is None:
        return default
    try:
        return int(v)
    except (TypeError, ValueError):
        return default


def _as_str(v: Any, default: str) -> str:
    """Coerce to str, falling back to ``default`` when missing."""
    if v is None:
        return default
    return str(v)


def _opt_str(v: Any) -> Optional[str]:
    """Pass through a string-ish optional, keeping ``None`` as ``None``."""
    if v is None:
        return None
    return str(v)


def _opt_int(v: Any) -> Optional[int]:
    """Pass through an int-ish optional, keeping ``None`` as ``None``."""
    if v is None:
        return None
    try:
        return int(v)
    except (TypeError, ValueError):
        return None


def _opt_float(v: Any) -> Optional[float]:
    """Pass through a float-ish optional, keeping ``None`` as ``None``."""
    if v is None:
        return None
    try:
        return float(v)
    except (TypeError, ValueError):
        return None


def _opt_bool(v: Any) -> Optional[bool]:
    """Pass through a real JSON bool, keeping everything else as ``None``."""
    if isinstance(v, bool):
        return v
    return None
