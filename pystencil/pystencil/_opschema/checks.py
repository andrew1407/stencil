"""The generic value checks and the cross-field presence rules, as a mixin.

Mixed into :class:`pystencil._opschema.schema.Schema`; every method needs only the
resolved ``self.limits``/``self.regexes`` and :meth:`describe`.
"""

from __future__ import annotations

from typing import Any

from .._types import NoneType
from .path import (
  _bad,
  _child,
  _includes,
  _is_int,
  _is_num,
  _is_obj,
  _item,
  _js_str,
  _label,
  _quote_keys,
  _quote_list,
  _where,
)


class ValueChecks:
  """Type, range, enum, grammar and presence checks shared by every entry."""

  # ── value checks ──────────────────────────────────────────────────────────
  def __check_string(self, v: Any, spec: dict, path: dict, parent: (dict | NoneType)) -> None:
    if not isinstance(v, str):
      _bad("%s must be a string" % _label(path))
    mx = self.limit(spec["maxChars"]) if spec.get("maxChars") is not None else self.limits["MAX_STRING_CHARS"]
    if len(v) > mx:
      _bad("%s is longer than %s characters" % (_label(path), _js_str(mx)))
    s = v.strip() if spec.get("trim") else v
    if spec.get("nonEmpty") and not s.strip():
      _bad("%s must be a non-empty string" % _label(path))
    if "enum" in spec and s not in spec["enum"]:
      _bad("%s must be one of %s" % (_label(path), _quote_list(spec["enum"])))
    if "literals" in spec and s in spec["literals"]:
      return
    if spec.get("blankOk") and not s.strip():
      return
    names = spec.get("regex") or []
    if isinstance(names, str):
      names = [names]
    if "regexBy" in spec:
      by = parent.get(spec["regexBy"]["key"]) if parent else None
      name = spec["regexBy"]["map"].get(by) if isinstance(by, str) else None
      names = [name] if name else []
    if names and not any(self.regexes[n].search(s) for n in names):
      _bad("%s must be %s" % (_label(path), " or ".join(self.describe(n) for n in names)))
    if spec.get("regexNot") and self.regexes[spec["regexNot"]].search(s):
      _bad("%s must be a local value, not %s" % (_label(path), self.describe(spec["regexNot"])))

  def __check_number(self, v: Any, spec: dict, path: dict) -> None:
    integer = spec["type"] == "integer"
    noun = "an integer" if integer else "a number"
    if not (_is_int(v) if integer else _is_num(v)):
      _bad("%s must be %s" % (_label(path), noun))
    if "enum" in spec and not _includes(spec["enum"], v):
      _bad("%s must be one of %s" % (_label(path), _quote_list(spec["enum"])))
    if spec.get("range"):
      lo, hi = spec["range"]
      if (lo is not None and v < lo) or (hi is not None and v > hi):
        if lo is not None and hi is not None:
          rng = "%s..%s" % (_js_str(lo), _js_str(hi))
        else:
          rng = ">= %s" % _js_str(lo) if lo is not None else "<= %s" % _js_str(hi)
        _bad("%s must be %s %s" % (_label(path), noun, rng))

  def __check_boolean(self, v: Any, spec: dict, path: dict) -> None:
    if not isinstance(v, bool):
      _bad("%s must be a boolean" % _label(path))
    if "enum" in spec and not _includes(spec["enum"], v):
      _bad("%s must be %s" % (_label(path), _quote_list(spec["enum"])))

  def __check_array(self, v: Any, spec: dict, path: dict) -> None:
    if not isinstance(v, list):
      _bad("%s must be an array" % _label(path))
    mn = self.limit(spec["minItems"]) if spec.get("minItems") is not None else None
    mx = self.limit(spec["maxItems"]) if spec.get("maxItems") is not None else None
    if mn == 1 and not v:
      _bad("%s must be a non-empty array" % _label(path))
    window = mn is not None and mn > 1 and mx is not None  # a real N..M window, not just a cap
    held = "%s must hold %s..%s entries" % (_label(path), _js_str(mn), _js_str(mx))
    if mx is not None and len(v) > mx:
      _bad(held if window else "more than %s entries in %s" % (_js_str(mx), _label(path)))
    if mn is not None and len(v) < mn:
      _bad(held if window else "%s must hold at least %s entries" % (_label(path), _js_str(mn)))
    if spec.get("items"):
      for i, x in enumerate(v):
        self._check_value(x, spec["items"], _item(path, i), None)

  def __check_object(self, v: Any, spec: dict, path: dict) -> None:
    if not _is_obj(v):
      _bad("%s must be an object" % _label(path))
    if spec.get("fields") or spec.get("minFields") is not None:
      self._check_fields(v, spec.get("fields") or {}, spec, path, [])

  def _check_value(self, v: Any, spec: dict, path: dict, parent: (dict | NoneType)) -> None:
    t = spec["type"]
    if t == "string":
      self.__check_string(v, spec, path, parent)
    elif t in ("integer", "number"):
      self.__check_number(v, spec, path)
    elif t == "boolean":
      self.__check_boolean(v, spec, path)
    elif t == "array":
      self.__check_array(v, spec, path)
    elif t == "object":
      self.__check_object(v, spec, path)
    else:
      raise ValueError('opRegistry: unknown type "%s"' % t)

  # One object against a key map + its holder's presence rules. `skip` names keys
  # that are neither declared nor unknown (the action's own "op").
  def _check_fields(self, obj: dict, fields: dict, holder: dict, path: (dict | NoneType), skip: list) -> None:
    def present(k: str) -> bool:
      return obj.get(k) is not None

    def at(k: str) -> dict:
      return _child(path, k)

    if not holder.get("allowUnknown"):
      for k in obj:
        if k not in skip and k not in fields:
          _bad('unknown field "%s"%s' % (k, " in %s" % _where(path) if path else ""))
    forms = holder.get("forms")
    if forms:
      in_forms = {k for f in forms for k in f}
      given = [k for k in fields if k in in_forms and present(k)]
      matched = [f for f in forms if len(f) == len(given) and all(k in given for k in f)]
      if len(matched) != 1:
        _bad("exactly one of %s is required" % " / ".join(_quote_keys(f, "+") for f in forms))
    for group in holder.get("together") or []:
      n = sum(1 for k in group if present(k))
      if n and n != len(group):
        _bad("%s ride together" % _quote_keys(group, " and "))
    for group in holder.get("exclusive") or []:
      if sum(1 for k in group if present(k)) > 1:
        _bad("carries both %s — at most one of them" % _quote_keys(group, " and "))
    if holder.get("minFields") is not None:
      n = sum(1 for k in fields if present(k))
      if n < holder["minFields"]:
        _bad("needs at least %s of %s" % (
          "one" if holder["minFields"] == 1 else holder["minFields"], "/".join(fields)))
    for k, spec in fields.items():
      if not present(k):
        if spec.get("required"):
          _bad("%s is required" % _label(at(k)))
        for dep, vals in (spec.get("requiredWith") or {}).items():
          if _includes(vals, obj.get(dep)):
            _bad('%s is required with "%s" %s' % (_label(at(k)), dep, _quote_list([obj.get(dep)])))
        continue
      for dep, vals in (spec.get("onlyWith") or {}).items():
        if not _includes(vals, obj.get(dep)):
          _bad('%s only applies with "%s" %s' % (
            _label(at(k)), dep, " or ".join('"%s"' % x for x in vals)))
      self._check_value(obj[k], spec, at(k), obj)
