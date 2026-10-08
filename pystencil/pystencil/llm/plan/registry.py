"""The op registry (contract §13): one entry per op — its applier, typed normalizer and
prompt bullet. The normalizers run on core's normalized action and validate nothing.
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

  def normalize(out: dict) -> dict:
    out[key] = out[key].strip()
    return out

  return normalize


def __normalize_save(out: dict) -> dict:
  """An empty path is no destination at all."""
  if out.get("path") == "":
    del out["path"]
  return out


def __normalize_open_url(out: dict) -> dict:
  """``incognito`` is False when omitted."""
  out.setdefault("incognito", False)
  return out


@dataclass(frozen=True)
class OpSpec:
  """One §13 registry entry. The prompt's op sections are generated from the table execution
  dispatches on, so the prompt never promises an op this surface cannot run."""

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


# (applier, normalizer, bullet scope); table order is prompt order (§4, then §10).
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
  "connect": (_apply_console_op, __strip_field("server"), "console"),
  "disconnect": (_apply_console_op, __strip_field("server"), "console"),
  "delete": (_apply_console_op, __strip_field("path"), "console"),
  "openUrl": (_apply_console_op, __normalize_open_url, "console"),
  "clear": (_apply_console_op, None, "console"),
  # Deferred to the end of the turn (the REPL's plan_clear_chat hook records it).
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
    top_level_only=bool(_flags.get("topLevelOnly")),
    console_settings=bool(_flags.get("editorSetting") or _flags.get("consoleSetting")),
  )
_unbound = sorted(set(ENTRIES) - set(OP_REGISTRY))
if _unbound:  # pragma: no cover - guards registry edits
  raise AssertionError(
    "opRegistry.json registers %s for pystencil, but nothing here executes them"
    % ", ".join(_unbound)
  )


# §13: never model-drivable; refused here at import and again by _apply_action.
FORBIDDEN_OPS = tuple(REGISTRY["forbidden"]["perSurface"]["pystencil"])

_forbidden_registered = sorted(set(OP_REGISTRY) & set(FORBIDDEN_OPS))
if _forbidden_registered:  # pragma: no cover - guards future registry edits
  raise AssertionError(
    "FORBIDDEN_OPS names may never be registered: %s" % ", ".join(_forbidden_registered)
  )


_ACTION_APPLIERS = {name: spec.applier for name, spec in OP_REGISTRY.items()}
