"""The history depth cap (`_MAX_STATES`).

An editing session is unbounded in length, so the snapshot stack is not: a new edit past
the cap evicts the OLDEST edit and never the pristine state, so `reset()` keeps working
after any number of edits. The number itself is shared — see `_snapshot.py`.
"""

from __future__ import annotations

from tests.helpers.nativecase import NativeCase

from pystencil.editor import Editor
from pystencil.editor._snapshot import _MAX_STATES


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


if __name__ == "__main__":
  import unittest

  unittest.main()
