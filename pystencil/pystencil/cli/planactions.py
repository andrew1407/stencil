"""One lowered ``.stc`` op as one action in the op-plan vocabulary of
``common/config/llm/opRegistry.json`` — the wire names ``applyPlanAction`` executes. Pure
builders: which actions a block needs, and in what frame, is ``plansequence.py``'s. Twin of
``cli/src/script/plan/{actions,crop}.zig``.
"""

from __future__ import annotations

import decimal

from .._script import PX_PER_CM
from .._ffi.types import NoneType

# core/parse/lengthTokens.cpp's absolute units: ``value * mul / div`` cm.
_CM_UNITS = {"cm": (1.0, 1.0), "mm": (1.0, 10.0), "in": (2.54, 1.0)}
_KEYS = ("x1", "x2", "y1", "y2")


def num(v: float) -> (int | float):
  """An integral value as a JSON integer, as the CLI's writer does."""
  return int(v) if float(v).is_integer() and abs(v) < 1e15 else v


def decimal_text(v: float) -> str:
  """Zig's ``{d}``: the shortest digits that read back as ``v``, never an exponent."""
  text = repr(float(v))
  if "e" in text: text = format(decimal.Decimal(text), "f")
  return text[:-2] if text.endswith(".0") else text


def open_action(target: str, from_url: bool) -> dict:
  return {"op": "openUrl", "url": target} if from_url else {"op": "openFile", "path": target}


def number_action(name: str, key: str, n: int) -> dict:
  return {"op": name, key: n}


def save_action(target: str) -> dict:
  return {"op": "save", "path": target} if target else {"op": "save"}


def filter_action(mode: str, tint: str, core) -> dict:
  out = {"op": "filter", "mode": mode}
  if mode == "custom": out["tint"] = _hex(tint, core)
  return out


def _hex(token: str, core) -> str:
  """A tint in the registry's HEX grammar; the raw token when the core cannot parse it."""
  parsed = core.parse_color(token) if core is not None else None
  return "#%02x%02x%02x" % parsed[:3] if parsed else token


def line_value(shape: dict, dx: float, dy: float) -> dict:
  """One drawn line as a layout line object, its image-pixel points moved by (dx, dy)."""
  pts = shape["points"]
  out = {
    "points": [{"x": num(pts[k] + dx), "y": num(pts[k + 1] + dy)}
               for k in range(0, len(pts) - 1, 2)],
    "color": shape["color"],
    "style": shape["style"],
    "fillColor": shape["fill_color"],
  }
  if shape["point_color"]: out["pointColor"] = shape["point_color"]
  out.update(thickness=num(shape["thickness"]), pointSize=num(shape["point_size"]),
             locked=shape["locked"])
  return out


def lines_action(shapes: list, dx: float, dy: float) -> dict:
  """A ``layout`` setting the drawn lines to ``shapes``, every point moved by (dx, dy)."""
  return {"op": "layout", "lines": [line_value(s, dx, dy) for s in shapes]}


def _split(tok: str) -> (tuple | NoneType):
  """A lowered length token as ``(from_end, value, unit)``."""
  from_end = tok.startswith("-")
  end = start = int(from_end)
  while end < len(tok) and (tok[end].isdigit() or tok[end] == "."): end += 1
  try:
    return from_end, float(tok[start:end]), tok[end:]
  except ValueError:
    return None


def _px_token(tok: str) -> str:
  """An edge with an absolute unit rewritten as the px ``--script`` resolves it to."""
  parts = _split(tok)
  if parts is None or parts[2] not in _CM_UNITS: return tok
  mul, div = _CM_UNITS[parts[2]]
  return ("-" if parts[0] else "") + decimal_text(parts[1] * mul / div * PX_PER_CM) + "px"


def _axis_px(tok: str, length: float, base: float) -> (float | NoneType):
  """core resolveAxisPx: a token's position on an axis ``length`` long, a bare number moving ``base``."""
  parts = _split(tok)
  if parts is None: return None
  from_end, value, unit = parts
  if not unit: return base + (-value if from_end else value)
  if unit == "px": px = value
  elif unit == "%": px = (value / 100.0) * length
  elif unit in _CM_UNITS: px = value * _CM_UNITS[unit][0] / _CM_UNITS[unit][1] * PX_PER_CM
  else: return None
  return length - px if from_end else px


def _derived_edge(toks, dims: tuple, x_given: bool, album: bool) -> (float | NoneType):
  """The far edge of the axis a one-axis crop leaves out (core resolveCropRect): it starts at 0
  and spans the given axis at the page's aspect, the page being the image at PX_PER_CM."""
  page_w, page_h = dims[0] / PX_PER_CM, dims[1] / PX_PER_CM
  lo, hi = min(page_w, page_h), max(page_w, page_h)
  aspect = 1.0 if lo <= 0.0 or hi <= 0.0 else (hi / lo if album else lo / hi)
  if aspect <= 0.0: aspect = 1.0
  length = dims[0] if x_given else dims[1]
  t1, t2 = (toks[0], toks[1]) if x_given else (toks[2], toks[3])
  e1 = _axis_px(t1, length, 0.0) if t1 else 0.0
  e2 = _axis_px(t2, length, length) if t2 else length
  if e1 is None or e2 is None: return None
  return abs(e2 - e1) / aspect if x_given else abs(e2 - e1) * aspect


def crop_action(op, dims: (tuple | NoneType)) -> dict:
  """A ``@crop`` in tokens an executor reads as ``--script`` does, never by its own page."""
  toks = tuple(op.toks) + ("",) * (4 - len(op.toks))
  x_given, y_given = bool(toks[0] or toks[1]), bool(toks[2] or toks[3])
  album = bool(op.num_at(0))
  far = None
  if x_given != y_given and dims: far = _derived_edge(toks, dims, x_given, album)
  spec = dict()
  for k, key in enumerate(_KEYS):
    if toks[k]: spec[key] = _px_token(toks[k])
    elif far is not None: spec[key] = "0px" if k % 2 == 0 else decimal_text(far) + "px"
  if op.str_at(0): spec["aspect"] = op.str_at(0)
  if album and far is None: spec["album"] = True
  return {"op": "crop", "spec": spec}
