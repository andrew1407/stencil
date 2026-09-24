"""Test-count floor for pystencil's suite.

Discovery still finds at least ``TEST_COUNT_FLOOR`` cases, so a stale pattern, a lost
directory or a filter typo cannot pass as "0 failed" while running a fraction of the suite.
"""

from __future__ import annotations

import unittest
from pathlib import Path


def _repo_root():
  """Walk up from this file to the checkout root so paths read the same everywhere."""
  for candidate in Path(__file__).resolve().parents:
    if (candidate / ".git").exists():
      return candidate
  raise RuntimeError("repo root not found above " + str(__file__))


REPO_ROOT = _repo_root()

# A floor, not a pin: raise it when the suite grows a lot; additions must never trip it.
TEST_COUNT_FLOOR = 630


def count_cases(suite):
  """Leaf TestCase instances in a discovered suite."""
  return sum(count_cases(t) if isinstance(t, unittest.TestSuite) else 1 for t in suite)


class TestCountFloorTests(unittest.TestCase):
  """A suite can report no failures while running a fraction of its tests — a stale
  pattern, a lost directory, a filter typo. This asserts discovery still finds them."""

  def test_discovery_finds_at_least_the_floor(self):
    found = count_cases(
      unittest.defaultTestLoader.discover(
        str(Path(__file__).resolve().parent), top_level_dir=str(REPO_ROOT / "pystencil")
      )
    )
    self.assertGreaterEqual(
      found,
      TEST_COUNT_FLOOR,
      "pystencil suite collapsed to %d tests, floor is %d" % (found, TEST_COUNT_FLOOR),
    )


if __name__ == "__main__":
  unittest.main()
