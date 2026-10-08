"""The editing snapshot and the small value types the whole facade shares."""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Union

from .._ffi.types import NoneType
from ..image import Image
from ..layout import Layout, Line


# History depth cap: the pristine state plus 63 undoable edits. Canon is LIMITS.historyMax
# in common/config/constants.json; cli/src/console/session/state.zig carries the same.
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
  """One editing state, the twin of the Zig ``EditState``: ``mirrored`` mirrors the original
  left-right FIRST, then ``rotation`` turns it 0..3 clockwise quarters; ``crop`` is ``(x, y, w, h)``
  in rotated-original pixel space or ``None``; ``filter_mode`` is "none"|"bw"|"sepia"|"custom"|
  "invert"|"contour", ``filter_color`` the custom hex."""

  rotation: int = 0
  mirrored: bool = False
  crop: (tuple[int, int, int, int] | NoneType) = None
  filter_mode: str = ""
  filter_color: str = ""
  lines: list[Line] = field(default_factory=list)

  def copy(self) -> "_Snapshot":
    """A shallow-but-safe clone: the line list is copied so appends don't alias."""
    return _Snapshot(
      rotation=self.rotation,
      mirrored=self.mirrored,
      crop=self.crop,
      filter_mode=self.filter_mode,
      filter_color=self.filter_color,
      lines=list(self.lines),
    )


def _clean_keywords(kw) -> list[str]:
  """Trimmed keywords, empties and non-strings dropped — port of project/file.js ``cleanKeywords``;
  no dedupe or case-folding, so a ``.stencil`` round-trip keeps the exact tag list."""
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


LoadSource = Union[str, bytes, bytearray, Image]
LayoutLike = Union[Layout, dict, str, list[Line]]
