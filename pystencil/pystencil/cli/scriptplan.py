from __future__ import annotations

"""``--script-plan``: a parsed ``.stc`` lowered to the op-plan JSON adapters already read.

One plan per ``MAX_ACTIONS`` actions, in the wire names of
``browser/js/config/llm/opRegistry.json``. Nothing is fetched and nothing is written — a
header-only size probe of each block's first local input is the only I/O. Twin of
``cli/src/script/{plan,planActions}.zig``.
"""

import json

from .. import codecs
from .._script import Script, ScriptError
from ..editor.source import _SourceApi
from ..scriptpaths import expand_source, resolve_target

VERSION = 1
# A copy of the op-plan envelope's limits.MAX_ACTIONS; tests/test_script_cli.py pins them.
MAX_ACTIONS = 16
_PROBE_BYTES = 4 << 20


def probe_dims(path: str, core) -> (tuple | None):
  """``(w, h)`` from an image header, or ``None`` for a URL, a video or an unreadable file."""
  if not path or _SourceApi._is_url(path): return None
  try:
    with open(path, "rb") as handle:
      return codecs.image_dimensions(handle.read(_PROBE_BYTES))
  except OSError:
    return None


def _open_action(target: str, is_url: bool) -> dict:
  return {"op": "openUrl", "url": target} if is_url else {"op": "openFile", "path": target}


def _crop_action(op) -> dict:
  spec = {k: v for k, v in zip(("x1", "x2", "y1", "y2"), op.toks) if v}
  if op.str_at(0): spec["aspect"] = op.str_at(0)
  if op.num_at(0): spec["album"] = True
  return {"op": "crop", "spec": spec}


def _filter_action(op, core) -> dict:
  mode = op.str_at(0)
  out = {"op": "filter", "mode": mode}
  if mode == "custom": out["tint"] = _hex(op.str_at(1), core)
  return out


def _hex(token: str, core) -> str:
  """A tint in the registry's HEX grammar; the raw token when the core cannot parse it."""
  parsed = core.parse_color(token) if core is not None else None
  return "#%02x%02x%02x" % parsed[:3] if parsed else token


def _num(v: float):
  return int(v) if float(v).is_integer() and abs(v) < 1e15 else v


def _line_value(op, program: Script, dims: tuple) -> (dict | None):
  """One ``@line``/``@rect`` as a layout line object, its points already in image pixels."""
  try:
    resolved = program.resolve(op.index, dims[0], dims[1])
  except ScriptError:
    return None
  if len(resolved) < 4: return None
  coords = resolved[:-2]
  return {
    "points": [{"x": _num(coords[i]), "y": _num(coords[i + 1])}
               for i in range(0, len(coords) - 1, 2)],
    "color": op.str_at(0),
    "style": op.str_at(1),
    "fillColor": op.str_at(2),
    "thickness": _num(resolved[-2]),
    "pointSize": _num(resolved[-1]),
    "locked": op.kind == "rect",
  }


def _flush(out: list, pending: list) -> None:
  """Burn the accumulated shapes into one ``layout`` action, as ``@save`` burns pixels.
  An empty pending list writes nothing — an empty ``lines`` array would CLEAR them."""
  if not pending: return
  out.append({"op": "layout", "lines": list(pending)})
  del pending[:]


def _crop_dims(op, program: Script, cur: tuple) -> tuple:
  try:
    resolved = program.resolve(op.index, cur[0], cur[1])
  except ScriptError:
    return cur
  if len(resolved) < 4 or resolved[2] <= 0 or resolved[3] <= 0: return cur
  return (resolved[2], resolved[3])


def build_actions(program: Script, block, first_input: str, dims: (tuple | None),
                  core=None) -> list:
  """The actions one block lowers to, in order. Without ``dims`` the shape ops are
  dropped rather than resolved against a size nobody has."""
  out: list = list()
  pending: list = list()
  for op in program.block_ops(block):
    if op.kind == "open":
      if first_input: out.append(_open_action(first_input, block.kind == "url"))
    elif op.kind == "frame":
      out.append({"op": "frame", "index": int(op.num_at(0))})
    elif op.kind == "crop":
      _flush(out, pending)
      out.append(_crop_action(op))
      if dims: dims = _crop_dims(op, program, dims)
    elif op.kind == "filter":
      out.append(_filter_action(op, core))
    elif op.kind in ("line", "rect"):
      value = _line_value(op, program, dims) if dims else None
      if value: pending.append(value)
    elif op.kind == "layout":
      if op.str_at(1) == "replace": del pending[:]
      out.append(_open_action(op.str_at(0), int(op.num_at(0, 1.0)) == 2))
    elif op.kind == "save":
      _flush(out, pending)
      out.append({"op": "save", "path": op.str_at(0)} if op.str_at(0) else {"op": "save"})
    elif op.kind in ("undo", "redo"):
      out.append({"op": op.kind, "steps": max(1, int(op.num_at(0, 1.0)))})
  _flush(out, pending)
  return out


def _saves(program: Script, block, inputs: list) -> list:
  """The concrete files a ``@save`` writes, one entry per input × save op."""
  out = list()
  for path in inputs:
    ext = codecs.format_from_ext(path) or "png"
    frame = block.frame or None
    for op in program.block_ops(block):
      if op.kind == "frame":
        frame = int(op.num_at(0))
      elif op.kind == "save":
        out.append({"input": path, "path": resolve_target(op.str_at(0), path, frame, ext)})
  return out


def _block_entry(program: Script, block, source: (str | None), core) -> dict:
  inputs = [source] if block.kind == "project" and source else list()
  if block.kind != "project":
    try:
      inputs = expand_source(block.source, block.kind)
    except ScriptError:
      inputs = list()
  first = inputs[0] if inputs else ""
  dims = probe_dims(first, core)
  actions = build_actions(program, block, first, dims, core)
  return {
    "index": block.index,
    "source": block.source,
    "sourceKind": block.kind,
    "inputs": inputs,
    "frame": block.frame,
    "dims": {"width": int(dims[0]), "height": int(dims[1])} if dims else None,
    "plans": [{"reply": "", "actions": actions[i:i + MAX_ACTIONS]}
              for i in range(0, len(actions), MAX_ACTIONS)],
    "saves": _saves(program, block, inputs),
  }


def lower_to_plan(program: Script, label: str, source: (str | None) = None,
                  core=None) -> str:
  """The whole envelope as one JSON line. An error lowers to no blocks — nothing in a
  script that does not parse cleanly is safe to act on."""
  blocks = list() if program.has_errors else [
    _block_entry(program, b, source, core) for b in program.blocks
  ]
  return json.dumps({
    "version": VERSION,
    "script": label,
    "diagnostics": [
      {"severity": d.severity, "code": d.code, "line": d.line, "col": d.col,
       "len": d.length, "message": d.message}
      for d in program.diagnostics
    ],
    "blocks": blocks,
  }, separators=(",", ":"), ensure_ascii=False)
