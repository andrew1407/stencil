"""The derived-view memo on :meth:`Editor.result`.

``result()`` rebuilds the whole view (rotate → crop → filter → one rasterize per line),
and ``save``/``layout``/``prompt`` all go through it — so it is memoized on the editor's
``revision`` counter. These tests drive a spying Core to prove a repeat call recomputes
nothing, that every state move invalidates the memo, and that callers still get their own
buffer. Needs the native core, like ``test_editor.py``.
"""

from __future__ import annotations

import unittest

from tests.helpers.nativecase import NativeCase

from pystencil.editor import Editor
from pystencil.layout import Layout, Line, Point


class _SpyCore:
  """Counts the core calls an Editor makes, delegating everything to the real one."""

  def __init__(self, core):
    self._core = core
    self.calls = dict()

  def __getattr__(self, name):
    attr = getattr(self._core, name)
    if not callable(attr):
      return attr

    def counted(*args, **kwargs):
      self.calls[name] = self.calls.get(name, 0) + 1
      return attr(*args, **kwargs)

    return counted

  @property
  def total(self):
    return sum(self.calls.values())

  def reset(self):
    self.calls = dict()


def _layout():
  """A one-stroke layout to rasterize over the view."""
  line = Line(points=[Point(1.0, 1.0), Point(15.0, 12.0)], color="#ff0000", thickness=2)
  return Layout(image_width=20, image_height=20, lines=[line])


class ResultCacheTests(NativeCase):
  def _editor(self):
    """A blank editor with a rotate + crop + filter + line already applied."""
    spy = _SpyCore(self.core)
    ed = Editor(core=spy)
    ed.blank(40, 30)
    ed.rotate(1).crop(x1=0, y1=0, x2=20, y2=20).set_filter("bw")
    ed.draw(_layout())
    return ed, spy

  def test_second_call_recomputes_nothing(self):
    ed, spy = self._editor()
    first = ed.result()
    spy.reset()
    second = ed.result()
    self.assertEqual(spy.total, 0, "result() re-derived the view: %r" % (spy.calls,))
    self.assertEqual(bytes(second.data), bytes(first.data))
    self.assertEqual((second.width, second.height), (first.width, first.height))

  def test_each_caller_gets_its_own_buffer(self):
    ed, _ = self._editor()
    first = ed.result()
    first.data[0] = (first.data[0] + 7) & 0xFF
    self.assertNotEqual(bytes(ed.result().data), bytes(first.data))

  def test_an_edit_invalidates_the_memo(self):
    ed, spy = self._editor()
    before = ed.result()
    ed.rotate(1)
    spy.reset()
    after = ed.result()
    self.assertGreater(spy.total, 0)
    self.assertEqual((after.width, after.height), (before.height, before.width))

  def test_undo_redo_and_reset_invalidate_the_memo(self):
    # Every cursor move bumps the revision, so the next result() re-derives — checked
    # by content, since a state with nothing to apply makes no core calls at all.
    ed, _ = self._editor()
    with_line = bytes(ed.result().data)
    ed.undo()
    self.assertNotEqual(bytes(ed.result().data), with_line)
    ed.redo()
    self.assertEqual(bytes(ed.result().data), with_line)
    ed.reset()
    pristine = ed.result()
    self.assertEqual((pristine.width, pristine.height), (40, 30))

  def test_with_lines_false_is_a_separate_key(self):
    ed, spy = self._editor()
    lined = ed.result()
    spy.reset()
    bare = ed.result(with_lines=False)
    self.assertGreater(spy.total, 0, "with_lines=False served the lined memo")
    self.assertNotEqual(bytes(bare.data), bytes(lined.data))
    # The memo holds one slot, so a repeat of the SAME key is what stays free.
    spy.reset()
    ed.result(with_lines=False)
    self.assertEqual(spy.total, 0)

  def test_a_new_source_invalidates_the_memo(self):
    ed, _ = self._editor()
    ed.result()
    ed.blank(11, 7)
    again = ed.result()
    self.assertEqual((again.width, again.height), (11, 7))


class LayoutDoesNotRenderTests(NativeCase):
  """``layout()`` needs the view's DIMENSIONS, not its pixels."""

  def test_layout_reports_result_dims_without_rasterizing(self):
    spy = _SpyCore(self.core)
    ed = Editor(core=spy)
    ed.blank(40, 30).rotate(1).crop(x1=0, y1=0, x2=20, y2=25)
    ed.draw(_layout())
    img = ed.result()
    spy.reset()
    layout = ed.layout()
    self.assertEqual(layout.image_width, img.width)
    self.assertEqual(layout.image_height, img.height)
    self.assertNotIn("rasterize_line", spy.calls)
    self.assertNotIn("crop_image_rgba", spy.calls)

  def test_layout_without_an_image_still_raises(self):
    with self.assertRaises(RuntimeError):
      Editor(core=_SpyCore(self.core)).layout()


if __name__ == "__main__":
  unittest.main()
