"""Registry-driven op-plan schema engine (llm-contract.md §1–§2, §8, §11).

A rule-for-rule port of ``browser/js/llm/opSchema.js`` over the checked-in copy of
``browser/js/config/llm/opRegistry.json`` (``tests/test_canonical_drift.py`` byte-pins
the copy): profile membership, unknown-field rejection, required keys, types, enums,
ranges, string caps, token grammars and the cross-field presence rules (forms /
together / exclusive / minFields / onlyWith / requiredWith). :mod:`pystencil.llm` keeps
only its normalizers, executors and the native rules an entry names. ``re`` + ``json``
only; the registry is parsed once, on first use.
"""

from __future__ import annotations

import importlib.resources
import json
import math
import re
from typing import Any, Callable, Dict, List, Optional


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
    if isinstance(x, bool):
        return "true" if x else "false"
    if x is None:
        return "null"
    if isinstance(x, float) and x.is_integer():
        return str(int(x))
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


def _child(p: Optional[dict], key: str) -> dict:
    if p and p["key"]:
        return {"root": "", "key": key, "container": _where(p)}
    return {"root": p["root"] if p else "", "key": key, "container": None}


def _item(p: dict, i: int) -> dict:
    return {"root": p["root"], "key": "%s[%d]" % (p["key"], i), "container": p["container"]}


# ── native cross-field rules an entry may name in `rules` ─────────────────────
def _crop_aspect_fold(a: dict) -> dict:
    """§3.2 tolerance: "aspect" beside "spec" folds into the spec when it lacks one; a
    conflicting duplicate fails. The folded copy is what gets validated + normalized."""
    if a.get("aspect") is None or not _is_obj(a.get("spec")):
        return a
    spec = dict(a["spec"])
    if spec.get("aspect") is not None and spec["aspect"] != a["aspect"]:
        _bad('"aspect" appears both beside "spec" and inside it with different values')
    if spec.get("aspect") is None:
        spec["aspect"] = a["aspect"]
    out = {k: v for k, v in a.items() if k != "aspect"}
    out["spec"] = spec
    return out


_RULES: Dict[str, Callable[[dict], dict]] = {"cropAspectFold": _crop_aspect_fold}


class Schema:
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
        self.entries: List[dict] = sorted(
            (
                self._resolve(e)
                for e in registry["ops"]
                if profile in e["profiles"] and (not e.get("surfaces") or surface in e["surfaces"])
            ),
            key=lambda e: order.index(e["name"]),
        )
        self.ops: Dict[str, dict] = {e["name"]: e for e in self.entries}
        self.forbidden = frozenset(registry["forbidden"]["perSurface"].get(surface, []))

    def _for_surface(self, table: Optional[dict]) -> Any:
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

    # ── value checks ──────────────────────────────────────────────────────────
    def _check_string(self, v: Any, spec: dict, path: dict, parent: Optional[dict]) -> None:
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

    def _check_number(self, v: Any, spec: dict, path: dict) -> None:
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

    def _check_boolean(self, v: Any, spec: dict, path: dict) -> None:
        if not isinstance(v, bool):
            _bad("%s must be a boolean" % _label(path))
        if "enum" in spec and not _includes(spec["enum"], v):
            _bad("%s must be %s" % (_label(path), _quote_list(spec["enum"])))

    def _check_array(self, v: Any, spec: dict, path: dict) -> None:
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

    def _check_object(self, v: Any, spec: dict, path: dict) -> None:
        if not _is_obj(v):
            _bad("%s must be an object" % _label(path))
        if spec.get("fields") or spec.get("minFields") is not None:
            self._check_fields(v, spec.get("fields") or {}, spec, path, [])

    def _check_value(self, v: Any, spec: dict, path: dict, parent: Optional[dict]) -> None:
        t = spec["type"]
        if t == "string":
            self._check_string(v, spec, path, parent)
        elif t in ("integer", "number"):
            self._check_number(v, spec, path)
        elif t == "boolean":
            self._check_boolean(v, spec, path)
        elif t == "array":
            self._check_array(v, spec, path)
        elif t == "object":
            self._check_object(v, spec, path)
        else:
            raise ValueError('opRegistry: unknown type "%s"' % t)

    # One object against a key map + its holder's presence rules. `skip` names keys
    # that are neither declared nor unknown (the action's own "op").
    def _check_fields(self, obj: dict, fields: dict, holder: dict, path: Optional[dict], skip: list) -> None:
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
        out: dict = {}
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


_SCHEMAS: Dict[str, Schema] = {}


def schema(surface: str = "pystencil") -> Schema:
    """The registry resolved for ``surface``, parsed once on first use."""
    s = _SCHEMAS.get(surface)
    if s is None:
        s = _SCHEMAS[surface] = Schema(load_registry(), surface)
    return s
