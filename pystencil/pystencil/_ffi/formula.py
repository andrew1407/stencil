"""The named values a coordinate formula may read besides its own axis.

Twin of ``core/parse/formulaContext.hpp`` and ``browser/js/core/parse/formulaContext.js``.
Page fields are always cm and image fields always pixels; ``unit`` only picks the spelling
``PAGE_WIDTH`` / ``PAGE_HEIGHT`` report in.
"""

from __future__ import annotations

import ctypes
from dataclasses import dataclass

from .marshal import _encode

# Not supplied: that name stays unknown, so the formula is invalid and apply() returns its input.
UNSET = float("nan")


@dataclass
class FormulaContext:
  """One set of named values. Every field defaults to UNSET, so a caller supplies only what
  it actually knows — a context with no image leaves IMAGE_WIDTH / IMAGE_HEIGHT unresolvable."""

  x: float = UNSET
  y: float = UNSET
  page_width_cm: float = UNSET
  page_height_cm: float = UNSET
  image_width: float = UNSET
  image_height: float = UNSET
  unit: str = "cm"  # "in" converts; every other word reads as cm


def ctx_args(ctx: FormulaContext) -> tuple:
  """Flatten a context into the trailing arguments of the *FormulaCtx ABI calls."""
  return (
    ctypes.c_double(ctx.x),
    ctypes.c_double(ctx.y),
    ctypes.c_double(ctx.page_width_cm),
    ctypes.c_double(ctx.page_height_cm),
    ctypes.c_double(ctx.image_width),
    ctypes.c_double(ctx.image_height),
    _encode(ctx.unit or "cm"),
  )
