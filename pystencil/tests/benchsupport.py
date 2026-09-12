"""Timing helpers for the opt-in benchmark suite (``tests/bench_*.py``).

``unittest discover`` matches ``test*.py``, so the normal run never executes these and no
timing assertion can gate a merge on a loaded machine. Run them on demand:

    python3 -m unittest discover -s tests -p "bench_*.py"
    python3 -m unittest tests.bench_codecs              # one file

Every assertion is RELATIVE — a ratio between two measurements, or how one scales as its
input doubles — never a wall-clock ceiling, so it means the same here and on CI. The µs/op
are printed, not asserted, and each ceiling names the algorithmic property it guards.
"""

from __future__ import annotations

import timeit

from tests.nativecase import NativeCase

REPS = 5


class BenchCase(NativeCase):
    """Measurement + ratio reporting. Native because most hot paths cross the ABI."""

    def micros(self, label: str, iterations: int, body) -> float:
        """Best-of-:data:`REPS` µs per invocation, after a warm-up pass.

        The best (fastest) run is kept rather than the mean: it is the one least
        contaminated by another process taking the core away mid-measurement.
        """
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
