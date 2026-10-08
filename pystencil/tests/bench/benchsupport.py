"""Timing helpers for the opt-in benchmarks, run with ``python3 -m unittest discover -s tests
-p "bench_*.py"``; the default ``test*.py`` discovery never runs them.

Every assertion is a ratio between two measurements, never a wall-clock ceiling.
"""

from __future__ import annotations

import timeit

from tests.helpers.nativecase import NativeCase

REPS = 5


class BenchCase(NativeCase):
  """Measurement + ratio reporting. Native because most hot paths cross the ABI."""

  def micros(self, label: str, iterations: int, body) -> float:
    """Best-of-:data:`REPS` µs per invocation, after a warm-up pass; the fastest run is the
    one least contaminated by another process."""
    timeit.timeit(body, number=min(iterations, 200))
    best = min(timeit.timeit(body, number=iterations) for _ in range(REPS))
    per = best / iterations * 1e6
    print("  %-52s %9.3f us/op  (best of %d x %d)" % (label, per, REPS, iterations))
    return per

  def ratio(self, label: str, slower: float, faster: float, ceiling: float) -> float:
    """Report a ratio and hold its ceiling — the assertion every bench here makes."""
    self.assertGreater(faster, 0.0, "%s: the baseline measured as zero" % label)
    got = slower / faster
    print("    %-50s %8.2fx  (ceiling %.2fx)" % (label, got, ceiling))
    self.assertLess(
      got, ceiling, "%s came out %.2fx, over the %.2fx ceiling" % (label, got, ceiling)
    )
    return got
