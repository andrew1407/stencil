"""Shared base for the .stencil project-file suites (``test_projectfile*.py``).

Saving and reopening a project renders the derived view, so these need the native
core; without it the case self-skips.
"""

from __future__ import annotations

from tests.helpers.nativecase import NativeCase

from pystencil.editor import Editor


class ProjectFileCase(NativeCase):
  """A project-file case over one authored editor state."""

  def _authored(self) -> Editor:
    """A blue blank with a name/colour/provenance, one quarter-turn, and a drawn line."""
    ed = Editor().blank(20, 12, color="#3060c0")
    ed._name = "proj"
    ed._color = "#7c3aed"
    ed._source = "https://example.com/a.png"
    ed.rotate_right()  # rotationQuarters = 1
    ed.draw({"lines": [{"points": [{"x": 1, "y": 1}, {"x": 5, "y": 5}], "color": "#ff0000"}]})
    return ed
