from __future__ import annotations

"""The package's one bounded fan-out primitive.

Threads, not processes: every caller's work either waits on a socket or spends its time
inside a ctypes call into the core, and both release the GIL. Each caller brings its own
worker bound, because what the bound protects differs (sockets against one host vs
cores).
"""

from concurrent.futures import ThreadPoolExecutor


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
