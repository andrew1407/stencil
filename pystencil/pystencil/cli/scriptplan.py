"""``--script-plan``: a parsed ``.stc`` lowered to the op-plan JSON adapters already read.

Each block's plans (laid out by ``plansequence.py``), beside its lowered ops with every length
as written, and again per input. Nothing is fetched or written: a header-only probe of each
local input and the local documents a ``@layout`` names are the only I/O. Twin of
``cli/src/script/plan.zig`` and ``cli/src/script/plan/{block,diagnostics}.zig``.
"""

from __future__ import annotations

import json

from .. import _net, codecs
from .._script import Script, ScriptError
from .._ffi.types import NoneType
from ..scriptpaths import expand_source, is_url, resolve_target, save_format
from .planactions import num
from .plansequence import PlanRefused, build

VERSION = 1
_PROBE_BYTES = 4 << 20


def probe_dims(path: str, core) -> (tuple | NoneType):
  """``(w, h)`` from a local image header, or ``None`` for a URL or an unreadable file."""
  if not path or is_url(path): return None
  try:
    with open(path, "rb") as handle:
      return codecs.image_dimensions(handle.read(_PROBE_BYTES))
  except OSError:
    return None


def _size(program: Script, block, path: str, core) -> (tuple | NoneType):
  """``path``'s size; a URL only when its block draws lines, through the guarded fetch the run
  opens it with. A URL that must be sized and does not fetch refuses the block."""
  if not is_url(path): return probe_dims(path, core)
  draw = next((op for op in program.block_ops(block) if op.kind in ("line", "rect", "layout")), None)
  if draw is None: return None
  try:
    dims = codecs.image_dimensions(_net._fetch(path, strict=False))
  except (OSError, ValueError):
    dims = None
  if dims: return dims
  raise PlanRefused(draw, "E_PLAN_SOURCE_UNREADABLE", "the lines this block draws need the size "
                    "of '%s', which could not be fetched as an image" % path)


def _saves(program: Script, block, inputs: list) -> list:
  """The concrete files a ``@save`` writes, one entry per input × save op."""
  out = list()
  for path in inputs:
    ext = save_format(codecs.format_from_ext(path))
    frame = block.frame or None
    for op in program.block_ops(block):
      if op.kind == "frame":
        frame = int(op.num_at(0))
      elif op.kind == "save":
        out.append({"input": path, "path": resolve_target(op.str_at(0), path, frame, ext)})
  return out


def _dims(dims: (tuple | NoneType)) -> (dict | NoneType):
  return {"width": int(dims[0]), "height": int(dims[1])} if dims else None


def _plans(plans: list) -> list:
  return [{"reply": "", "actions": actions} for actions in plans]


def _input_entry(program: Script, block, path: str, dims: (tuple | NoneType), core) -> dict:
  """One input with its own size, the plans resolved against it (opening it), and its saves."""
  return {
    "input": path,
    "dims": _dims(dims),
    "plans": _plans(build(program, block, path, dims, core)),
    "saves": _saves(program, block, [path]),
  }


def _op_entry(op) -> dict:
  """One lowered op (stc-contract §13), its length tokens unresolved for any input."""
  return {"kind": op.kind, "line": op.line, "edit": op.edit_index, "strs": list(op.strs),
          "toks": list(op.toks), "nums": [num(v) for v in op.nums]}


def _block_entry(program: Script, block, source: (str | NoneType), core) -> dict:
  inputs = [source] if block.kind == "project" and source else list()
  if block.kind != "project":
    try:
      inputs = expand_source(block.source, block.kind)
    except ScriptError:
      inputs = list()
  first = inputs[0] if inputs else ""
  dims = _size(program, block, first, core)
  return {
    "index": block.index,
    "source": block.source,
    "sourceKind": block.kind,
    "inputs": inputs,
    "frame": block.frame,
    "dims": _dims(dims),
    "plans": _plans(build(program, block, first, dims, core)),
    "saves": _saves(program, block, inputs),
    "ops": [_op_entry(op) for op in program.block_ops(block)],
    "perInput": [
      _input_entry(program, block, path, dims if k == 0 else _size(program, block, path, core), core)
      for k, path in enumerate(inputs)
    ],
  }


def _diagnostic(d, related: (tuple | NoneType)) -> dict:
  out = {"severity": d.severity, "code": d.code, "line": d.line, "col": d.col,
         "len": d.length, "message": d.message}
  if related: out["related"] = dict(zip(("line", "col", "len"), related))
  return out


def _diagnostics(program: Script, refused: (PlanRefused | NoneType)) -> list:
  """The script's diagnostics, and a refusal among them in source order, spanning the
  directive it stopped at."""
  out = [_diagnostic(d, r) for d, r in zip(program.diagnostics, program.related)]
  if refused is None: return out
  op = refused.op
  own = {"severity": "error", "code": refused.code, "line": op.line, "col": op.col,
         "len": 1 + len(op.kind), "message": refused.message}
  at = next((k for k, d in enumerate(out) if op.line < d["line"]), len(out))
  return out[:at] + [own] + out[at:]


def lower_to_plan(program: Script, label: str, source: (str | NoneType) = None,
                  core=None) -> tuple:
  """The whole envelope as one JSON line, and whether every block planned. An error lowers
  to no blocks — nothing in a script that does not parse cleanly is safe to act on — and so
  does a block the planner refuses, its reason a diagnostic among the rest."""
  if core is None:
    from ..core import get_core
    core = get_core()
  blocks, refused = list(), None
  if not program.has_errors:
    try:
      blocks = [_block_entry(program, b, source, core) for b in program.blocks]
    except PlanRefused as why:
      blocks, refused = list(), why
  return json.dumps({
    "version": VERSION,
    "script": label,
    "diagnostics": _diagnostics(program, refused),
    "blocks": blocks,
  }, separators=(",", ":"), ensure_ascii=False), not program.has_errors and refused is None
