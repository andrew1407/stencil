"""The package's parallel fan-outs (`_net._fetch_all`).

Scraping a gallery, measuring candidate images and polling several servers are all pure
I/O waits, so they run together instead of one after another. Every test here proves the
concurrency with a ``threading.Barrier``: the jobs only get past it if they are genuinely
in flight at the same time, so a regression to a serial loop fails instead of just being
slower. Ordering, naming and error reporting must stay exactly as the serial code left it.
"""

from __future__ import annotations

import io
import tempfile
import threading
import unittest
from pathlib import Path

from pystencil import _net, codecs, server, sitesource
from pystencil._net import MAX_FETCH_WORKERS, _fetch_all
from pystencil.sitesource import MediaItem, download_media

_TIMEOUT = 10.0  # generous: the barrier only has to be reached, not raced


def _png(width: int, height: int) -> bytes:
  return codecs.encode_png(width, height, bytearray(width * height * 4))


class FetchAllTests(unittest.TestCase):
  def test_results_come_back_in_input_order(self):
    got = _fetch_all(range(8), lambda n: n * n)
    self.assertEqual(got, [0, 1, 4, 9, 16, 25, 36, 49])

  def test_jobs_run_concurrently(self):
    barrier = threading.Barrier(MAX_FETCH_WORKERS, timeout=_TIMEOUT)

    def job(n):
      barrier.wait()  # BrokenBarrierError if the batch is run serially
      return n

    self.assertEqual(_fetch_all(range(MAX_FETCH_WORKERS), job), list(range(MAX_FETCH_WORKERS)))

  def test_a_single_job_runs_inline(self):
    who = list()
    _fetch_all(["only"], lambda j: who.append(threading.current_thread().name))
    self.assertEqual(who, [threading.current_thread().name])

  def test_the_pool_is_bounded(self):
    # More jobs than workers must still complete: each batch of MAX_FETCH_WORKERS
    # releases the barrier for the next.
    barrier = threading.Barrier(MAX_FETCH_WORKERS, timeout=_TIMEOUT)
    got = _fetch_all(range(MAX_FETCH_WORKERS * 3), lambda n: (barrier.wait(), n)[1])
    self.assertEqual(got, list(range(MAX_FETCH_WORKERS * 3)))


class _FetchStub:
  """Stands in for ``_net._fetch``: every call waits for the others before answering."""

  def __init__(self, parties, bodies, fail=()):
    self.barrier = threading.Barrier(parties, timeout=_TIMEOUT)
    self.bodies = bodies
    self.fail = set(fail)
    self.urls = list()
    self._lock = threading.Lock()

  def __call__(self, url, **kwargs):
    self.barrier.wait()
    with self._lock:
      self.urls.append(url)
    if url in self.fail:
      raise OSError("boom")
    return self.bodies[url]


class DownloadMediaConcurrencyTests(unittest.TestCase):
  """``download_media`` fetches together, then writes in input order."""

  def setUp(self):
    self._real_fetch = _net._fetch
    self.addCleanup(setattr, _net, "_fetch", self._real_fetch)

  def _items(self, n):
    return [
      MediaItem(url="http://example.com/pic-%d.png" % i, kind="img", ext="png")
      for i in range(n)
    ]

  def test_every_item_is_fetched_in_parallel_and_written_in_order(self):
    items = self._items(5)
    bodies = {it.url: _png(3 + i, 2) for i, it in enumerate(items)}
    _net._fetch = _FetchStub(len(items), bodies)
    with tempfile.TemporaryDirectory() as out:
      err = io.StringIO()
      paths = download_media(items, out, host="example.com", err=err)
    self.assertEqual([Path(p).name for p in paths], ["pic-%d.png" % i for i in range(5)])
    lines = err.getvalue().splitlines()
    self.assertEqual(len(lines), 5)
    self.assertIn("(3x2 px · source example.com)", lines[0])
    # The measured dimensions land back on the right items.
    self.assertEqual([(it.width, it.height) for it in items], [(3 + i, 2) for i in range(5)])

  def test_a_failed_fetch_reports_and_keeps_the_index(self):
    items = self._items(3)
    bodies = {it.url: _png(2, 2) for it in items}
    _net._fetch = _FetchStub(len(items), bodies, fail={items[1].url})
    with tempfile.TemporaryDirectory() as out:
      err = io.StringIO()
      paths = download_media(items, out, host="example.com", err=err)
    self.assertEqual([Path(p).name for p in paths], ["pic-0.png", "pic-2.png"])
    text = err.getvalue()
    self.assertIn("error: could not fetch http://example.com/pic-1.png (boom)", text)

  def test_custom_name_indexes_follow_the_input_order(self):
    items = self._items(3)
    bodies = {it.url: _png(2, 2) for it in items}
    _net._fetch = _FetchStub(len(items), bodies)
    with tempfile.TemporaryDirectory() as out:
      paths = download_media(items, out, host="example.com", name="photo")
    self.assertEqual(
      [Path(p).name for p in paths], ["photo-0.png", "photo-1.png", "photo-2.png"]
    )


class MeasureConcurrencyTests(unittest.TestCase):
  """The dimension filter measures its candidates together, not one by one."""

  def setUp(self):
    self._real_fetch = _net._fetch
    self.addCleanup(setattr, _net, "_fetch", self._real_fetch)

  def test_scan_page_measures_candidates_in_parallel(self):
    urls = ["http://example.com/a-%d.png" % i for i in range(4)]
    html = "".join('<img src="%s">' % u for u in urls)
    bodies = {u: _png(120 + i, 40) for i, u in enumerate(urls)}
    page = "http://example.com/gallery"
    bodies[page] = html.encode("utf-8")
    # The page fetch is its own (single) call, so the barrier only counts the media.
    stub = _FetchStub(len(urls), bodies)
    page_done = threading.Event()

    def fetch(url, **kwargs):
      if not page_done.is_set():
        page_done.set()
        return bodies[page]
      return stub(url, **kwargs)

    _net._fetch = fetch
    items = sitesource.scan_page(page, min_width=100)
    self.assertEqual([it.url for it in items], urls)
    self.assertEqual([it.width for it in items], [120 + i for i in range(4)])


class _StubConn:
  def __init__(self, barrier, projects=None, raises=None):
    self.barrier = barrier
    self.projects = projects or []
    self.raises = raises

  def list_projects(self):
    self.barrier.wait()
    if self.raises is not None:
      raise self.raises
    return self.projects


class RemoteProjectsConcurrencyTests(unittest.TestCase):
  """``ConnectionManager.remote_projects`` polls every server on one tick."""

  def _manager(self, conns):
    mgr = server.ConnectionManager()
    for i, conn in enumerate(conns):
      mgr._conns["http://s%d:8090" % i] = conn
    return mgr

  def test_servers_are_polled_together_and_aggregated_in_order(self):
    barrier = threading.Barrier(3, timeout=_TIMEOUT)
    conns = [_StubConn(barrier, [{"id": "s%d" % i}]) for i in range(3)]
    got = self._manager(conns).remote_projects()
    self.assertEqual(got, [{"id": "s0"}, {"id": "s1"}, {"id": "s2"}])

  def test_an_unreachable_server_is_skipped(self):
    barrier = threading.Barrier(3, timeout=_TIMEOUT)
    conns = [
      _StubConn(barrier, [{"id": "a"}]),
      _StubConn(barrier, raises=OSError("down")),
      _StubConn(barrier, [{"id": "c"}]),
    ]
    self.assertEqual(
      self._manager(conns).remote_projects(), [{"id": "a"}, {"id": "c"}]
    )

  def test_no_connections_is_empty(self):
    self.assertEqual(server.ConnectionManager().remote_projects(), [])


if __name__ == "__main__":
  unittest.main()
