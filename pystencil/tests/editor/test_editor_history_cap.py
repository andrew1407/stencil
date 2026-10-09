"""The history caps: the depth (`_MAX_STATES`) and the points kept over every state.

An editing session is unbounded in length, so the snapshot stack is not: a new edit past
the cap evicts the OLDEST edit and never the pristine state, so `reset()` keeps working
after any number of edits. The number itself is shared — see `_snapshot.py`.
"""

from __future__ import annotations

from tests.helpers.nativecase import NativeCase

from pystencil.editor import Editor
from pystencil.editor._snapshot import _MAX_STATES, _Snapshot, _evictions
from pystencil.layout import Line, Point


class HistoryCapTests(NativeCase):
  def _edited(self, edits):
    ed = Editor().blank(8, 8)
    for i in range(edits):
      ed.rotate(1)
    return ed

  def test_the_cap_is_the_shared_value(self):
    # Pinned against cli/src/console/session/state.zig's max_states.
    self.assertEqual(_MAX_STATES, 64)

  def test_the_stack_stops_growing_at_the_cap(self):
    ed = self._edited(_MAX_STATES * 3)
    self.assertEqual(len(ed._history), _MAX_STATES)

  def test_under_the_cap_every_edit_is_kept(self):
    ed = self._edited(_MAX_STATES - 5)
    self.assertEqual(len(ed._history), _MAX_STATES - 4)  # + the pristine state

  def test_the_cursor_follows_the_eviction(self):
    ed = self._edited(_MAX_STATES * 2)
    self.assertEqual(ed._cursor, len(ed._history) - 1)
    self.assertIs(ed._current(), ed._history[-1])

  def test_the_pristine_state_survives_every_eviction(self):
    ed = self._edited(_MAX_STATES * 2)
    self.assertEqual(ed.image_size, (8, 8))  # 4n quarter-turns = back to square
    ed.reset()
    self.assertEqual(len(ed._history), 1)
    self.assertEqual(ed._cursor, 0)

  def test_undo_walks_the_whole_retained_stack(self):
    ed = self._edited(_MAX_STATES * 2)
    steps = 0
    while ed.undo():
      steps += 1
    self.assertEqual(steps, _MAX_STATES - 1)
    self.assertEqual(ed._cursor, 0)

  def test_the_caps_are_cores(self):
    self.assertEqual(Editor()._get_core().history_caps(), (_MAX_STATES, 2_000_000))

  def test_evictions_take_the_oldest_edits_never_the_pristine_or_the_newest(self):
    def states(*pts):
      return [_Snapshot(points=n) for n in pts]
    self.assertEqual(_evictions(states(0, 3, 3), 4, 6), 0)
    self.assertEqual(_evictions(states(0, 3, 3, 1), 3, 100), 1)
    self.assertEqual(_evictions(states(0, 3, 3, 3), 64, 6), 1)
    self.assertEqual(_evictions(states(0, 3, 3, 9), 64, 6), 2)  # the newest alone stays
    self.assertEqual(_evictions(states(4, 1, 1, 1), 64, 5), 2)  # the pristine points count

  def test_a_push_past_the_points_cap_drops_the_oldest_edit(self):
    ed = Editor().blank(8, 8)
    core = ed._get_core()

    class SmallCap:
      def __getattr__(self, name):
        return getattr(core, name)

      def history_caps(self):
        return (_MAX_STATES, 5)

    ed._core = SmallCap()
    line = lambda n: Line(points=[Point(float(i), 1.0) for i in range(n)])
    ed.draw([line(3)], combine=False)
    ed.draw([line(2)], combine=False)
    self.assertEqual(len(ed._history), 3)
    self.assertEqual([s.points for s in ed._history], [0, 3, 2])
    ed.draw([line(4)], combine=False)
    self.assertEqual([s.points for s in ed._history], [0, 4])
    self.assertEqual(ed._cursor, 1)


if __name__ == "__main__":
  import unittest

  unittest.main()
