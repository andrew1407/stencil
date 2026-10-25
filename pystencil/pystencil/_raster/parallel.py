"""The package's one bounded fan-out primitive.

Threads, not processes: every caller's work either waits on a socket or spends its time
inside a ctypes call into the core, and both release the GIL. Each caller brings its own
worker bound, because what the bound protects differs (sockets against one host vs
cores).
"""

from __future__ import annotations

import os
from concurrent.futures import ThreadPoolExecutor

# A picture smaller than this (pixels) runs in one call: a band's thread costs more than it saves.
BAND_MIN_PIXELS = 1 << 20
# The CLI's band bound (cli/src/media/imageRows.zig): at most this many threads per kernel.
MAX_BANDS = 8


def map_parallel(jobs, work, max_workers: int) -> list:
  """Map ``work`` over ``jobs`` in a bounded pool; results come back in INPUT order.

  Each job must be self-contained (its own guard, its own failure handling) — this is a
  fan-out, not a scheduler: an exception from ``work`` propagates on iteration, at the
  position of the job that raised, so a failure looks the same as it did serially.
  A batch of one runs inline and costs no thread.
  """
  jobs = list(jobs)
  if len(jobs) < 2: return [work(job) for job in jobs]
  with ThreadPoolExecutor(max_workers=min(max_workers, len(jobs))) as pool:
    return list(pool.map(work, jobs))


def row_bands(width: int, height: int) -> list:
  """Half-open row ranges covering ``height``, one per thread, or a single range when small."""
  if height <= 0: return []
  n = 1 if width * height < BAND_MIN_PIXELS else max(1, min(MAX_BANDS, os.cpu_count() or 1, height))
  step = -(-height // n)
  return [(y, min(height, y + step)) for y in range(0, height, step)]
