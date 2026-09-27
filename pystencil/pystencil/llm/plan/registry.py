"""The op registry (contract §13): ONE entry per op, the single source of an op's
existence on this surface — its applier, typed normalizer and prompt bullet.

The per-op normalizers live here too: each runs on the action core already validated
and normalized, and is the typed detail this executor wants beyond the generic deep-pick.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Callable

from ..._ffi.types import NoneType
from ..console import _apply_console_op
from .limits import PROFILE, REGISTRY
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


# ── per-op normalizers: the typed struct the generic deep-pick can't express ──
# Each runs on core's normalized action; nothing here validates.
def __normalize_crop(out: dict) -> dict:
  """The console-only ``"album": false`` means "no derivation" — dropped from the spec."""
  if out["spec"].get("album") is False:
    del out["spec"]["album"]
  return out


def __float_dims(out: dict) -> dict:
  """§2 centimetre dims are floats (page / blank)."""
  for key in ("width", "height"):
    if key in out: out[key] = float(out[key])
  return out


Normalizer = Callable[[dict], dict]


def __strip_field(key: str) -> Normalizer:
  """Store a padded string field trimmed (connect/disconnect server, delete path);
  resolution against the console's own state happens at execution."""

  def normalize(out: dict) -> dict:
    out[key] = out[key].strip()
    return out

  return normalize


def __normalize_save(out: dict) -> dict:
  """An empty (or all-space) path is no destination at all; this executor
  notes+skips a path anyway."""
  if out.get("path") == "":
    del out["path"]
  return out


def __normalize_open_url(out: dict) -> dict:
  """``incognito`` always rides the action (False when omitted). Whether the USER
  wrote the URL is the plan-level guard's job (:func:`url_echoed_by_user`)."""
  out.setdefault("incognito", False)
  return out


# ── the op registry (contract §13): ONE entry per op ──────────────────────────
@dataclass(frozen=True)
class OpSpec:
  """One §13 registry entry — the single source of an op's existence on this surface.

  Membership, the flags and the prompt bullet come from the shared opRegistry.json
  entry; this surface adds the applier, its typed normalizer and the block that carries
  the bullet, so the "Available ops" prompt sections are GENERATED from the same table
  that execution dispatches on: the prompt can never promise an op this surface cannot run.
  """

  applier: Callable[..., None]
  bullet: str                     # the asset's prompt bullet, verbatim (§4/§10)
  normalizer: (Normalizer | NoneType) = None  # on core's normalized action, if any
  scope: str = "core"             # which block carries the bullet: "core"|"console"
  top_level_only: bool = False    # §2/§2.1: inside "variants" it drops that variant
  console_settings: bool = False  # §10 console profile: variant ban + console hooks
  capability: str = ""            # runtime capability the op needs ("" = always wired)


def __per_surface(table: (dict | NoneType)) -> (dict | NoneType):
  if not table: return None
  mine = table.get("pystencil")
  return mine if mine is not None else table.get(PROFILE)


def __entries() -> dict[str, dict]:
  """This surface's registry entries by name, in prompt order, bullet and flags resolved;
  core resolves the same way (``opplanSchemaEntries``), and a native test holds them equal."""
  order = REGISTRY["profiles"][PROFILE]["ops"]
  mine = [e for e in REGISTRY["ops"] if PROFILE in e["profiles"]
          and (not e.get("surfaces") or "pystencil" in e["surfaces"])]
  out: dict[str, dict] = dict()
  for e in sorted(mine, key=lambda e: order.index(e["name"])):
    variant = __per_surface(e.get("bulletVariants"))
    flags = dict(e.get("flags") or {}, **(__per_surface(e.get("surfaceFlags")) or {}))
    out[e["name"]] = {"bullet": variant if isinstance(variant, str) else e.get("bullet"), "flags": flags}
  return out


ENTRIES = __entries()


# What this surface adds to each registry entry: (applier, normalizer, bullet scope). Table
# order is prompt order: the §2 core ops as §4 lists them, then the §10 console ops.
_SURFACE_OPS: dict[str, tuple] = {
  "crop": (_apply_crop, __normalize_crop, "core"),
  "rotate": (_apply_rotate, None, "core"),
  "filter": (_apply_filter, None, "core"),
  "layout": (_apply_layout, None, "core"),
  "formula": (_apply_formula, None, "core"),
  "page": (_apply_page, __float_dims, "core"),
  "blank": (_apply_blank, __float_dims, "core"),
  "undo": (_apply_history_step, None, "core"),
  "redo": (_apply_history_step, None, "core"),
  "frame": (_apply_frame, None, "core"),
  "image": (_apply_image, None, "core"),
  "save": (_apply_save, __normalize_save, "core"),
  # The §10 console profile.
  "connect": (_apply_console_op, __strip_field("server"), "console"),
  "disconnect": (_apply_console_op, __strip_field("server"), "console"),
  "delete": (_apply_console_op, __strip_field("path"), "console"),
  "openUrl": (_apply_console_op, __normalize_open_url, "console"),
  "clear": (_apply_console_op, None, "console"),
  # clearChat runs via the /chat clear path with an in-app confirm, DEFERRED to the
  # end of the turn (the REPL's plan_clear_chat hook records it).
  "clearChat": (_apply_console_op, None, "console"),
  "reset": (_apply_reset, None, "console"),
}

OP_REGISTRY: dict[str, OpSpec] = dict()
for _name, (_applier, _normalizer, _scope) in _SURFACE_OPS.items():
  _entry = ENTRIES.get(_name)
  if _entry is None:  # pragma: no cover - guards registry edits
    raise AssertionError('"%s" has no pystencil entry in opRegistry.json' % _name)
  _flags = _entry["flags"]
  OP_REGISTRY[_name] = OpSpec(
    # A bullet shared by two ops (undo/redo, connect/disconnect) sits on the
    # first entry; the partner's asset bullet is null and emits nothing.
    _applier, _entry["bullet"] or "", _normalizer, scope=_scope,
    # §2/§2.1 top-level-only ops drop the variant they appear in; the §10 settings ops do the
    # same (with their own message) and run through the console hooks.
    top_level_only=bool(_flags.get("topLevelOnly")),
    console_settings=bool(_flags.get("editorSetting") or _flags.get("consoleSetting")),
  )
_unbound = sorted(set(ENTRIES) - set(OP_REGISTRY))
if _unbound:  # pragma: no cover - guards registry edits
  raise AssertionError(
    "opRegistry.json registers %s for pystencil, but nothing here executes them"
    % ", ".join(_unbound)
  )


# §13 forbidden ops — the §10 "never model-drivable" boundary, as names. Two teeth: the
# import-time registry check below, and _apply_action's reject.
FORBIDDEN_OPS = tuple(REGISTRY["forbidden"]["perSurface"]["pystencil"])

_forbidden_registered = sorted(set(OP_REGISTRY) & set(FORBIDDEN_OPS))
if _forbidden_registered:  # pragma: no cover - guards future registry edits
  raise AssertionError(
    "FORBIDDEN_OPS names may never be registered: %s" % ", ".join(_forbidden_registered)
  )


# Derived dispatch tables (single source: OP_REGISTRY).
_ACTION_APPLIERS = {name: spec.applier for name, spec in OP_REGISTRY.items()}
