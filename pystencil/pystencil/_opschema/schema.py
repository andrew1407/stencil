"""The registry resolved for one surface: entries, limits, grammars, public checks."""

from __future__ import annotations

import importlib.resources
import json
import re
from typing import Any

from .._types import NoneType
from .checks import ValueChecks
from .path import SchemaError, _anchor, _bad, _is_num, _is_obj
from .rules import _RULES


class Schema(ValueChecks):
  """The registry resolved for one surface: its entries (profile = prompt order),
  forbidden set, limits, grammars, and the generic checks."""

  def __init__(self, registry: dict, surface: str) -> None:
    profile = registry["$meta"]["surfaceProfiles"].get(surface)
    if not profile:
      raise ValueError('opRegistry: unknown surface "%s"' % surface)
    self.registry = registry
    self.surface = surface
    self.profile = profile
    self.limits: dict = registry["limits"]
    self.envelope: dict = registry["envelope"]
    self.regexes = {
      name: re.compile(_anchor(src))
      for name, src in registry["regexes"].items()
      if name not in ("describe", "note")
    }
    order = registry["profiles"][profile]["ops"]
    # This surface's entries: the profile's ops in prompt order, minus entries
    # restricted to other surfaces, each resolved for this surface (surfaceKeys /
    # bulletVariants / surfaceFlags).
    self.entries: list[dict] = sorted(
      (
        self._resolve(e)
        for e in registry["ops"]
        if profile in e["profiles"] and (not e.get("surfaces") or surface in e["surfaces"])
      ),
      key=lambda e: order.index(e["name"]),
    )
    self.ops: dict[str, dict] = {e["name"]: e for e in self.entries}
    self.forbidden = frozenset(registry["forbidden"]["perSurface"].get(surface, []))

  def _for_surface(self, table: (dict | NoneType)) -> Any:
    if not table:
      return None
    return table.get(self.surface) if table.get(self.surface) is not None else table.get(self.profile)

  def _resolve(self, e: dict) -> dict:
    variant = self._for_surface(e.get("bulletVariants"))
    out = dict(e)
    surface_keys = (e.get("surfaceKeys") or {}).get(self.surface)  # {} is a real (field-less) map
    out["keys"] = surface_keys if surface_keys is not None else e["keys"]
    out["bullet"] = variant if isinstance(variant, str) else e.get("bullet")
    out["addendum"] = variant.get("addendum") if isinstance(variant, dict) else None
    out["flags"] = dict(e.get("flags") or {}, **(self._for_surface(e.get("surfaceFlags")) or {}))
    return out

  def describe(self, name: str) -> str:
    return self.registry["regexes"]["describe"].get(name, name)

  def limit(self, v: Any) -> Any:
    """A cap is a number or a dotted name into `limits` ("MAX_ACTIONS", "ask.label")."""
    if _is_num(v):
      return v
    n: Any = self.limits
    for k in str(v).split("."):
      n = n.get(k) if isinstance(n, dict) else None
    if not _is_num(n):
      raise ValueError('opRegistry: unknown limit "%s"' % v)
    return n
  # ── normalization: the declared keys only, defaults applied, trims honoured ──
  def _pick(self, v: Any, spec: dict) -> Any:
    if spec["type"] == "object" and spec.get("fields") and _is_obj(v):
      return self._pick_fields(v, spec["fields"])
    if spec["type"] == "array" and isinstance(v, list):
      return [self._pick(x, spec["items"]) for x in v] if spec.get("items") else list(v)
    if spec["type"] == "string" and spec.get("trim") and isinstance(v, str):
      return v.strip()
    return v

  def _pick_fields(self, obj: dict, fields: dict) -> dict:
    out: dict = dict()
    for k, spec in fields.items():
      if obj.get(k) is not None:
        out[k] = self._pick(obj[k], spec)
      elif "default" in spec:
        out[k] = spec["default"]
    return out

  # ── public surface ────────────────────────────────────────────────────────
  def validate_action(self, a: dict, entry: dict) -> dict:
    """Validate one action against its entry (native rules first). Returns the
    action as validated (post-fold) — feed it to :meth:`normalize`."""
    try:
      v = a
      for rule in entry.get("rules") or []:
        v = _RULES[rule](v)
      self._check_fields(v, entry["keys"], entry, None, ["op"])
      return v
    except SchemaError as e:
      raise SchemaError('invalid "%s" action: %s' % (entry["name"], e)) from None

  def normalize(self, v: dict, entry: dict) -> dict:
    """``{op, declared keys present (deep-picked), defaults}``."""
    out = {"op": entry["name"]}
    out.update(self._pick_fields(v, entry["keys"]))
    return out

  def validate_ask(self, ask: Any) -> None:
    """The §11 card's structure (option ``actions`` only shallowly — the caller
    validates them as preview actions)."""
    if not _is_obj(ask):
      _bad('"ask" must be an object')
    schema = self.registry["ask"]["schema"]
    self._check_fields(ask, schema["keys"], schema, {"root": "ask.", "key": "", "container": None}, [])

  def normalize_ask(self, ask: dict) -> dict:
    return self._pick_fields(ask, self.registry["ask"]["schema"]["keys"])

  def opset_entry(self, name: str, op: str) -> Any:
    """Resolve an op inside a nested op set (§8 open.actions): an entry to validate
    with, ``"fail"`` for a listed-but-disallowed op, or None for an unknown op."""
    os_ = self.registry["opsets"].get(name)
    if os_ is None:
      raise ValueError('opRegistry: unknown opset "%s"' % name)
    override = (os_.get("overrides") or {}).get(op)
    if override:
      return {"name": op, "keys": override["keys"], "rules": override.get("rules") or []}
    if op in os_["ops"]:
      return next((e for e in self.registry["ops"] if e["id"] == op), None)
    if op in (os_.get("failOps") or []):
      return "fail"
    return None

  def check_envelope(self, v: Any, key: str) -> None:
    """Check one envelope slot ("actions" / "variants") shallowly."""
    self._check_value(v, self.envelope[key], {"root": "", "key": key, "container": None}, None)


def load_registry() -> dict:
  """The checked-in copy of the canonical registry, parsed fresh."""
  return json.loads(
    importlib.resources.files("pystencil")
    .joinpath("_data/opRegistry.json")
    .read_text(encoding="utf-8")
  )


_SCHEMAS: dict[str, Schema] = dict()


def schema(surface: str = "pystencil") -> Schema:
  """The registry resolved for ``surface``, parsed once on first use."""
  s = _SCHEMAS.get(surface)
  if s is None:
    s = _SCHEMAS[surface] = Schema(load_registry(), surface)
  return s
