from __future__ import annotations

"""The op registry (contract §13): ONE entry per op, the single source of an op's
existence on this surface — its key schema, validator, applier and prompt bullet.

The per-op normalizers live here too: each runs on the registry-normalized action,
after the table-driven check passed, and is the typed detail the generic deep-pick
cannot express.
"""

from dataclasses import dataclass
from typing import Callable

from .._types import NoneType
from .._opschema import SchemaError
from .console import _apply_console_op
from .errors import LlmPlanError
from .limits import SCHEMA
from .ops import (
  _apply_blank,
  _apply_crop,
  _apply_filter,
  _apply_formula,
  _apply_frame,
  _apply_history_step,
  _apply_image,
  _apply_layout,
  _apply_page,
  _apply_reset,
  _apply_rotate,
  _apply_save,
)


# ── per-op normalizers: the typed struct filling the generic deep-pick can't express ──
# Each runs on the registry-normalized action — {op, declared keys present, defaults,
# `trim` keys trimmed} — AFTER the table-driven check passed; nothing here validates.
def _normalize_crop(out: dict) -> dict:
  """The console-only ``"album": false`` means "no derivation" — dropped from the spec."""
  if out["spec"].get("album") is False:
    del out["spec"]["album"]
  return out


def _float_dims(out: dict) -> dict:
  """§2 centimetre dims are floats (page / blank)."""
  for key in ("width", "height"):
    if key in out:
      out[key] = float(out[key])
  return out


Normalizer = Callable[[dict], dict]


def _strip_field(key: str) -> Normalizer:
  """Store a padded string field trimmed (connect/disconnect server, delete path);
  resolution against the console's own state happens at execution."""

  def normalize(out: dict) -> dict:
    out[key] = out[key].strip()
    return out

  return normalize


def _normalize_save(out: dict) -> dict:
  """An empty (or all-space) path is no destination at all; this executor
  notes+skips a path anyway."""
  if out.get("path") == "":
    del out["path"]
  return out


def _normalize_open_url(out: dict) -> dict:
  """``incognito`` always rides the action (False when omitted). Whether the USER
  wrote the URL is the plan-level guard's job (:func:`url_echoed_by_user`)."""
  out.setdefault("incognito", False)
  return out


# ── the op registry (contract §13): ONE entry per op ──────────────────────────
@dataclass(frozen=True)
class OpSpec:
  """One §13 registry entry — the single source of an op's existence on this surface.

  Membership, the key schema and the flags come from the shared opRegistry.json entry
  (SCHEMA), the prompt bullet included; this surface adds the validator (the
  table-driven check + normalize), the applier and the block that carries the bullet,
  so the "Available ops" prompt sections are GENERATED from the same table that
  validation and execution dispatch on: the prompt can never promise an op this
  surface cannot run.
  """

  validator: Normalizer
  applier: Callable[..., None]
  fields: frozenset[str]          # allowed action keys (including "op" itself)
  bullet: str                     # the asset's prompt bullet, verbatim (§4/§10)
  scope: str = "core"             # which block carries the bullet: "core"|"console"
  top_level_only: bool = False    # §2/§2.1: inside "variants" it drops that variant
  console_settings: bool = False  # §10 console profile: variant ban + console hooks
  capability: str = ""            # runtime capability the op needs ("" = always wired)


def _make_validator(
  entry: dict, normalizer: (Normalizer | NoneType)
) -> Normalizer:
  """The registry's check (native rules, unknown fields, types, grammars, presence
  rules) + generic normalize, then this surface's own normalizer, if any."""

  def validate(a: dict) -> dict:
    try:
      out = SCHEMA.normalize(SCHEMA.validate_action(a, entry), entry)
    except SchemaError as e:
      raise LlmPlanError(str(e)) from None
    return normalizer(out) if normalizer is not None else out

  return validate


# What this surface adds to each registry entry: (applier, normalizer, bullet scope).
# The bullet prose itself comes from the shared opRegistry.json entry, so a canonical
# reword lands here untouched. Table order is prompt order: the §2 core ops as §4 lists
# them, then the §10 console-profile ops as the console block splices them (reset last —
# a §2 core op whose bullet rides the console block). The pystencil console carries the
# cli console's profile minus accent/reconnect (no theme, no reconnect command) and copy
# (no clipboard) — the registry restricts those entries to the cli, so they are never
# registered or promised here.
_SURFACE_OPS: dict[str, tuple] = {
  "crop": (_apply_crop, _normalize_crop, "core"),
  "rotate": (_apply_rotate, None, "core"),
  "filter": (_apply_filter, None, "core"),
  "layout": (_apply_layout, None, "core"),
  "formula": (_apply_formula, None, "core"),
  "page": (_apply_page, _float_dims, "core"),
  "blank": (_apply_blank, _float_dims, "core"),
  "undo": (_apply_history_step, None, "core"),
  "redo": (_apply_history_step, None, "core"),
  "frame": (_apply_frame, None, "core"),
  "image": (_apply_image, None, "core"),
  "save": (_apply_save, _normalize_save, "core"),
  # The §10 console profile.
  "connect": (_apply_console_op, _strip_field("server"), "console"),
  "disconnect": (_apply_console_op, _strip_field("server"), "console"),
  "delete": (_apply_console_op, _strip_field("path"), "console"),
  "openUrl": (_apply_console_op, _normalize_open_url, "console"),
  "clear": (_apply_console_op, None, "console"),
  # clearChat runs via the /chat clear path with an in-app confirm, DEFERRED to the
  # end of the turn (the REPL's plan_clear_chat hook records it).
  "clearChat": (_apply_console_op, None, "console"),
  "reset": (_apply_reset, None, "console"),
}

OP_REGISTRY: dict[str, OpSpec] = {}
for _name, (_applier, _normalizer, _scope) in _SURFACE_OPS.items():
  _entry = SCHEMA.ops.get(_name)
  if _entry is None:  # pragma: no cover - guards registry edits
    raise AssertionError('"%s" has no pystencil entry in opRegistry.json' % _name)
  _flags = _entry["flags"]
  OP_REGISTRY[_name] = OpSpec(
    _make_validator(_entry, _normalizer), _applier,
    # A bullet shared by two ops (undo/redo, connect/disconnect) sits on the
    # first entry; the partner's asset bullet is null and emits nothing.
    frozenset({"op", *_entry["keys"]}), _entry["bullet"] or "", scope=_scope,
    # §2/§2.1 top-level-only ops drop the variant they appear in; the §10
    # settings ops do the same (with their own message) and run through the
    # console hooks.
    top_level_only=bool(_flags.get("topLevelOnly")),
    console_settings=bool(_flags.get("editorSetting") or _flags.get("consoleSetting")),
  )
_unbound = sorted(set(SCHEMA.ops) - set(OP_REGISTRY))
if _unbound:  # pragma: no cover - guards registry edits
  raise AssertionError(
    "opRegistry.json registers %s for pystencil, but nothing here executes them"
    % ", ".join(_unbound)
  )


# §13 forbidden ops — the §10 "never model-drivable" boundary, as NAMES (the
# registry's forbidden.perSurface.pystencil): the assistant's own configuration
# (self-configuration is the exfiltration primitive), clipboard READS, hotkey
# rebinding, ending the session, chat persistence/consent toggles, and server-side
# destruction beyond §10's grants. Two teeth: the import-time registry check
# below (parse skips an unregistered name as unknown), and _apply_action's reject.
FORBIDDEN_OPS = tuple(SCHEMA.registry["forbidden"]["perSurface"]["pystencil"])

_forbidden_registered = sorted(set(OP_REGISTRY) & set(FORBIDDEN_OPS))
if _forbidden_registered:  # pragma: no cover - guards future registry edits
  raise AssertionError(
    "FORBIDDEN_OPS names may never be registered: %s" % ", ".join(_forbidden_registered)
  )


# Derived dispatch tables (single source: OP_REGISTRY).
_ACTION_FIELDS = {name: spec.fields for name, spec in OP_REGISTRY.items()}
_ACTION_VALIDATORS = {name: spec.validator for name, spec in OP_REGISTRY.items()}
_ACTION_APPLIERS = {name: spec.applier for name, spec in OP_REGISTRY.items()}
_TOP_LEVEL_ONLY_OPS = tuple(n for n, s in OP_REGISTRY.items() if s.top_level_only)
_CONSOLE_SETTINGS_OPS = tuple(n for n, s in OP_REGISTRY.items() if s.console_settings)
