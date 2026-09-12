"""Editor.result timings: the derived-view pipeline and the memo in front of it.

`result()` is the only renderer — save, layout, prompt and every variant go through it —
so it runs rotate → crop → filter → one rasterize per line, and is memoized on the
editor's revision counter. The ceilings guard that shape: one pass per stage, one pass per
line, and a repeat between edits that costs a buffer copy.
"""

from __future__ import annotations

from tests.benchsupport import BenchCase

from pystencil.editor import Editor
from pystencil.layout import Layout, Line, Point


def _lines(count: int, points: int, width: int, height: int) -> Layout:
  """`count` strokes of `points` vertices each, spread across the view."""
  made = []
  for i in range(count):
    y = (i * height) // max(count, 1)
    pts = [Point(float((j * width) // max(points - 1, 1)), float(y)) for j in range(points)]
    made.append(Line(points=pts, color="#ff0000", thickness=2.0))
  return Layout(image_width=width, image_height=height, lines=made)


def _editor(width, height, *, lines=0, points=2, rotate=0, crop=False, filter_mode=""):
  ed = Editor().blank(width, height, color="#3060c0")
  if rotate:
    ed.rotate(rotate)
  if crop:
    ed.crop("x1=10% y1=10% x2=90% y2=90%")
  if filter_mode:
    ed.set_filter(filter_mode)
  if lines:
    ed.draw(_lines(lines, points, width, height))
  return ed


class ResultBench(BenchCase):
  def test_the_memo_makes_a_repeat_render_a_copy(self):
    ed = _editor(900, 700, lines=8, rotate=1, crop=True, filter_mode="bw")

    def fresh():
      ed._result = None
      ed.result()

    uncached = self.micros("Editor.result (memo dropped each call)", 60, fresh)
    cached = self.micros("Editor.result (memo warm)", 300, ed.result)

    # The whole point of memoizing on `revision`: between edits a repeat render is one
    # buffer copy, not rotate + crop + filter + a rasterize per line.
    self.ratio("warm vs cold", cached, uncached, ceiling=0.5)

  def test_an_edit_invalidates_the_memo_and_nothing_else_does(self):
    ed = _editor(900, 700, lines=8, filter_mode="bw")
    ed.result()
    rotated = iter(range(1 << 30))

    def after_edit():
      ed.rotate(1 if next(rotated) % 2 == 0 else 3)
      ed.result()

    warm = self.micros("Editor.result (memo warm)", 300, ed.result)
    edited = self.micros("rotate + Editor.result (memo dropped)", 60, after_edit)
    # An edit bumps the revision, so the next render really re-derives.
    self.ratio("warm vs after an edit", warm, edited, ceiling=0.5)
    # Reading a property is not an edit: the revision is untouched, so the memo holds.
    def probe():
      ed.image_size
      ed.page_format
      ed.result()

    probed = self.micros("image_size + page_format + result", 300, probe)
    self.ratio("property reads vs a bare warm render", probed, warm, ceiling=2.5)

  def test_rendering_is_linear_in_the_pixel_count(self):
    base = _editor(600, 400, rotate=1, crop=True, filter_mode="bw")
    wide = _editor(1200, 400, rotate=1, crop=True, filter_mode="bw")
    tall = _editor(600, 800, rotate=1, crop=True, filter_mode="bw")

    def render(ed):
      def run():
        ed._result = None
        ed.result()
      return run

    small = self.micros("result 600x400 (rotate+crop+bw)", 60, render(base))
    twice_wide = self.micros("result 1200x400", 40, render(wide))
    twice_tall = self.micros("result 600x800", 40, render(tall))

    # Each stage is one pass over the buffer, so twice the pixels is twice the work
    # whichever axis grew. A scattered row order would show up as a wider spread.
    self.ratio("twice the width", twice_wide, small, ceiling=3.0)
    self.ratio("twice the height", twice_tall, small, ceiling=3.0)

  def test_rasterizing_is_linear_in_the_stroke_count(self):
    few = _editor(800, 600, lines=10)
    many = _editor(800, 600, lines=20)
    longer = _editor(800, 600, lines=10, points=8)

    def render(ed):
      def run():
        ed._result = None
        ed.result()
      return run

    base = self.micros("result + 10 strokes of 2 points", 40, render(few))
    twice_lines = self.micros("result + 20 strokes of 2 points", 40, render(many))
    longer_us = self.micros("result + 10 strokes of 8 points", 40, render(longer))

    # One core rasterize_line call per stroke, each walking its own segments: twice the
    # strokes is at most twice the work, and 7 segments cost under 7 one-segment calls
    # (the per-call marshalling is paid once).
    self.ratio("twice the strokes", twice_lines, base, ceiling=3.0)
    self.ratio("7 segments vs 1", longer_us, base, ceiling=7.0)


if __name__ == "__main__":
  import unittest

  unittest.main()
