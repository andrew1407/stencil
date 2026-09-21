from __future__ import annotations

"""The editing snapshot and the small value types the whole facade shares.

``_Snapshot`` is the Python mirror of the Zig ``EditState``: rotation + crop + filter
+ lines, never baked eagerly — :meth:`Editor.result` derives the view from it.
"""

from dataclasses import dataclass, field
from typing import Union

from .._ffi.types import NoneType
from ..image import Image
from ..layout import Layout, Line


# History depth cap: the pristine state plus 63 undoable edits. Canon is LIMITS.historyMax
# in browser/js/config/constants.json; cli/src/console/session/state.zig carries the same.
_MAX_STATES = 64

# Fallback A4 page (cm) if the core has no named page table — matches pipeline.zig's
# `core.namedPageSize("A4") orelse core.Page{ .w = 21.0, .h = 29.7 }`.
_A4_FALLBACK = (21.0, 29.7)

# Image extension → MIME for the `.stencil` data URL (default image/png).
_EXT_MIME = {
  "jpg": "image/jpeg", "jpeg": "image/jpeg", "gif": "image/gif",
  "webp": "image/webp", "bmp": "image/bmp",
}
_BASE64_PREFIX = "base64,"


@dataclass
class _Snapshot:
  """One editing snapshot — the Python mirror of the Zig ``EditState``.

  ``rotation`` is 0..3 clockwise quarter-turns applied to the original FIRST; ``crop``
  is an ``(x, y, w, h)`` rect in rotated-original pixel space (or ``None``); the filter
  is a mode string ("none"|"bw"|"sepia"|"custom"|"invert"|"contour") plus a custom hex
  colour; ``lines`` is the list of drawn :class:`Line` objects.
  """

  rotation: int = 0
  crop: (tuple[int, int, int, int] | NoneType) = None
  filter_mode: str = ""
  filter_color: str = ""
  lines: list[Line] = field(default_factory=list)

  def copy(self) -> "_Snapshot":
    """A shallow-but-safe clone: the line list is copied so appends don't alias."""
    return _Snapshot(
      rotation=self.rotation,
      crop=self.crop,
      filter_mode=self.filter_mode,
      filter_color=self.filter_color,
      lines=list(self.lines),
    )


def _clean_keywords(kw) -> list[str]:
  """Trim keywords and drop empties/non-strings — port of project/file.js ``cleanKeywords``.

  Kept deliberately simple (no dedupe/lower-casing) so a ``.stencil`` round-trip preserves
  the exact tag list every other surface reads/writes.
  """
  if not isinstance(kw, list): return []
  return [k.strip() for k in kw if isinstance(k, str) and k.strip()]


def _sniff_image_ext(data: bytes) -> (str | NoneType):
  """Best-effort image format from magic bytes (for a lossless save when there's no filename)."""
  if data[:8] == b"\x89PNG\r\n\x1a\n": return "png"
  if data[:2] == b"\xff\xd8": return "jpg"
  if data[:6] in (b"GIF87a", b"GIF89a"): return "gif"
  if data[:4] == b"RIFF" and data[8:12] == b"WEBP": return "webp"
  if data[:2] == b"BM": return "bmp"
  return None


# Source object types accepted by Editor.load().
LoadSource = Union[str, bytes, bytearray, Image]
# Layout-ish inputs accepted by draw()/apply_layout().
LayoutLike = Union[Layout, dict, str, list[Line]]
