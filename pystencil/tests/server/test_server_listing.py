"""list_projects walks GET /projects through every nextCursor, and refuses a cursor loop."""

from __future__ import annotations

import unittest

from pystencil.server import ServerConnection, ServerError
from pystencil.server import projects as projects_mod


def _paged(conn, pages):
  """Stub the connection's _request with `pages` keyed by the ?after= cursor (None = first)."""
  calls = list()

  def fake(method, path, **kw):
    calls.append((method, path, kw))
    return pages[(kw.get("query") or {}).get("after")]

  conn._request = fake
  return calls


class ListProjectsPagingTest(unittest.TestCase):
  """Every page of the list, in order, with exactly the requests the walk needs."""

  def setUp(self) -> None:
    self.conn = ServerConnection("http://host:8090", token="tok123")

  def test_walks_every_page_in_order(self) -> None:
    calls = _paged(self.conn, {
      None: {"projects": [{"id": "p3"}, {"id": "p2"}], "nextCursor": "c1"},
      "c1": {"projects": [{"id": "p1"}], "nextCursor": "c2"},
      "c2": {"projects": [{"id": "p0"}]},
    })
    self.assertEqual([p["id"] for p in self.conn.list_projects()], ["p3", "p2", "p1", "p0"])
    self.assertEqual(calls, [
      ("GET", "/projects", {}),
      ("GET", "/projects", {"query": {"after": "c1"}}),
      ("GET", "/projects", {"query": {"after": "c2"}}),
    ])

  def test_a_single_page_is_one_request(self) -> None:
    calls = _paged(self.conn, {None: {"projects": [{"id": "p1"}], "nextCursor": ""}})
    self.assertEqual(self.conn.list_projects(), [{"id": "p1"}])
    self.assertEqual(calls, [("GET", "/projects", {})])

  def test_a_repeated_cursor_raises_instead_of_looping(self) -> None:
    calls = _paged(self.conn, {
      None: {"projects": [{"id": "p2"}], "nextCursor": "c1"},
      "c1": {"projects": [{"id": "p1"}], "nextCursor": "c1"},
    })
    with self.assertRaises(ServerError) as got:
      self.conn.list_projects()
    self.assertEqual(got.exception.code, "badResponse")
    self.assertEqual(len(calls), 2)

  def test_endless_paging_is_cut_off(self) -> None:
    served = list()

    def fake(method, path, **kw):
      served.append(kw)
      return {"projects": [], "nextCursor": "c%d" % len(served)}

    self.conn._request = fake
    with self.assertRaises(ServerError):
      self.conn.list_projects()
    self.assertEqual(len(served), projects_mod._MAX_LIST_PAGES)

  def test_the_cursor_is_urlencoded_as_after(self) -> None:
    urls = list()

    def stub_open(req, raw=False):
      urls.append(req.full_url)
      return {"projects": [], "nextCursor": "1700/p_1_a"} if len(urls) == 1 else {"projects": []}

    self.conn._open = stub_open
    self.conn.list_projects()
    self.assertEqual(urls, [
      "http://host:8090/projects",
      "http://host:8090/projects?after=1700%2Fp_1_a",
    ])


if __name__ == "__main__":
  unittest.main()
