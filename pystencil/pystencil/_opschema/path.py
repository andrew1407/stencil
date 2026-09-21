"""Type predicates, JS-shaped string rendering, and the value paths in error messages.

Ported from ``browser/js/llm/plan/opSchema.js``; a message must read the same on both
surfaces, so ``_js_str``/``_eq`` reproduce JS ``String()`` and strict equality rather
than Python's.
"""

from __future__ import annotations

import math
import re
from typing import Any

from .._types import NoneType


class SchemaError(Exception):
  """A check failed; the caller re-raises it as its own plan error."""


def _bad(why: str) -> None:
  raise SchemaError(why)


def _is_obj(v: Any) -> bool:
  return isinstance(v, dict)


def _is_num(v: Any) -> bool:
  return isinstance(v, (int, float)) and not isinstance(v, bool) and math.isfinite(v)


def _is_int(v: Any) -> bool:
  return _is_num(v) and float(v).is_integer()


def _eq(a: Any, b: Any) -> bool:
  """JS-style strict equality for enum / onlyWith membership (a bool never equals a number)."""
  if isinstance(a, bool) or isinstance(b, bool):
    return isinstance(a, bool) and isinstance(b, bool) and a == b
  return a == b


def _includes(vals: list, v: Any) -> bool:
  return any(_eq(x, v) for x in vals)


def _js_str(x: Any) -> str:
  """``String(x)`` as JS prints it, for messages."""
  if isinstance(x, bool): return "true" if x else "false"
  if x is None: return "null"
  if isinstance(x, float) and x.is_integer(): return str(int(x))
  return str(x)


def _quote_list(xs: list) -> str:
  return ", ".join('"%s"' % x if isinstance(x, str) else _js_str(x) for x in xs)


def _quote_keys(keys: list, sep: str) -> str:
  return sep.join('"%s"' % k for k in keys)


# A JS ``$`` only matches at the very end; Python's also matches before a final newline.
def _anchor(src: str) -> str:
  return src[:-1] + r"\Z" if src.endswith("$") and not src.endswith(r"\$") else src


# Where a value sits, for messages: ``"x1" in spec``, ``"label" in ask.options[2]``.
def _where(p: dict) -> str:
  if p["key"]:
    return ("%s." % p["container"] if p["container"] else "") + p["root"] + p["key"]
  return re.sub(r"\.$", "", p["root"])


def _label(p: dict) -> str:
  return '"%s%s"' % (p["root"], p["key"]) + (" in %s" % p["container"] if p["container"] else "")


def _child(p: (dict | NoneType), key: str) -> dict:
  if p and p["key"]: return {"root": "", "key": key, "container": _where(p)}
  return {"root": p["root"] if p else "", "key": key, "container": None}


def _item(p: dict, i: int) -> dict:
  return {"root": p["root"], "key": "%s[%d]" % (p["key"], i), "container": p["container"]}
