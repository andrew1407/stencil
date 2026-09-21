"""Shared base for the Editor facade suites (``test_editor*.py``).

These exercise the derived-view pipeline (rotate → crop → filter → rasterize) and the
history/cursor model against the REAL native core, so they need the shared library
built; without it the whole case self-skips — the editor is a thin orchestration layer
over the core ABI, so there is nothing meaningful to test.
"""

from __future__ import annotations

from tests.helpers.nativecase import NativeCase

from pystencil.editor import Editor


def _grayscale_pixels(data, count):
  """True when every pixel's R==G==B (the b&w filter collapses the channels)."""
  for i in range(count):
    d = i * 4
    if not (data[d] == data[d + 1] == data[d + 2]):
      return False
  return True


class EditorCase(NativeCase):
  """A native-core-backed editor case with the 32x48 blank most tests start from."""

  def _blank(self):
    """A fresh 32x48 blank editor used by most cases."""
    return Editor().blank(32, 48)
