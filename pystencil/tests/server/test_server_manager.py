"""Error parsing, the connection manager, and poll-based project-change tracking."""

from __future__ import annotations

import threading
import unittest

from pystencil.server import ConnectionManager, ServerConnection, ServerError, diff_projects
from pystencil.server import manager as manager_module

class ErrorParsingTest(unittest.TestCase):
  def test_server_error_fields(self) -> None:
    err = ServerError("conflict", "stale version", status=409)
    self.assertEqual(err.code, "conflict")
    self.assertEqual(err.message, "stale version")
    self.assertEqual(err.status, 409)


class ConnectionManagerUnitTest(unittest.TestCase):
  def test_normalizes_and_dedupes(self) -> None:
    mgr = ConnectionManager()
    # Directly seed the internal map (no network) to exercise the views.
    c = ServerConnection("https://a:8090", token="t")
    mgr._conns[c.base] = c
    self.assertEqual(mgr.connections, ["https://a:8090"])
    self.assertTrue(mgr.has("a:8090"))
    self.assertIs(mgr.get("https://a:8090/"), c)

  def test_disconnect_last(self) -> None:
    mgr = ConnectionManager()
    for url in ("http://a:8090", "http://b:8090"):
      conn = ServerConnection(url)
      mgr._conns[conn.base] = conn
    mgr.disconnect()  # drops most-recently added
    self.assertEqual(mgr.connections, ["http://a:8090"])


class _RecordingConn:
  """Stands in for ServerConnection: records the handshake instead of making one."""

  made: list = list()

  def __init__(self, base, token="", *, verify=True):
    self.base = base
    self.token = token
    self.verify = verify
    self.connected = 0
    self.closed = 0
    _RecordingConn.made.append(self)

  def connect(self):
    self.connected += 1
    self.token = self.token or "minted"  # a tokenless connect mints a session token
    return self

  def close(self):
    self.closed += 1


class ReconnectTest(unittest.TestCase):
  """``reconnect()`` drops the live set and rebuilds it from the remembered
  (url, token) pairs, so a server restart is recovered without re-typing anything."""

  def setUp(self):
    _RecordingConn.made = list()
    self.addCleanup(setattr, manager_module, "ServerConnection", ServerConnection)
    manager_module.ServerConnection = _RecordingConn

  def test_reconnect_rebuilds_the_last_set_with_its_tokens(self):
    mgr = ConnectionManager()
    mgr.connect([{"url": "http://a:8090", "token": "ta"}, "http://b:8090"])
    first = list(_RecordingConn.made)
    mgr.reconnect()
    self.assertEqual([c.closed for c in first], [1, 1])  # the old pair is closed
    rebuilt = _RecordingConn.made[2:]
    self.assertEqual([c.base for c in rebuilt], ["http://a:8090", "http://b:8090"])
    # The user's token and the minted one both ride along into the new connections.
    self.assertEqual([c.token for c in rebuilt], ["ta", "minted"])
    self.assertEqual([c.connected for c in rebuilt], [1, 1])
    self.assertEqual(mgr.connections, ["http://a:8090", "http://b:8090"])
    self.assertIsNot(mgr.get("http://a:8090"), first[0])

  def test_reconnect_with_nothing_connected_is_a_no_op(self):
    mgr = ConnectionManager()
    self.assertIs(mgr.reconnect(), mgr)
    self.assertEqual(mgr.connections, [])
    self.assertEqual(_RecordingConn.made, [])

  def test_reconnect_restores_the_set_as_of_the_last_connect(self):
    mgr = ConnectionManager()
    mgr.connect(["http://a:8090", "http://b:8090"])
    mgr.disconnect("http://b:8090")
    # _last is recorded by connect(), not by disconnect(), so b comes back.
    mgr.reconnect()
    self.assertEqual(mgr.connections, ["http://a:8090", "http://b:8090"])


class DiffProjectsTest(unittest.TestCase):
  """The pure diff that powers poll-based project-change watching."""

  def test_empty_inputs(self) -> None:
    self.assertEqual(diff_projects(None, None), [])
    self.assertEqual(diff_projects([], []), [])

  def test_created(self) -> None:
    curr = [{"id": "a", "name": "A", "color": "", "version": 1}]
    changes = diff_projects([], curr)
    self.assertEqual(len(changes), 1)
    self.assertEqual(changes[0]["kind"], "created")
    self.assertEqual(changes[0]["fields"], [])
    self.assertEqual(changes[0]["project"], curr[0])

  def test_deleted_carries_prior_record(self) -> None:
    prev = [{"id": "a", "name": "A", "version": 1}]
    changes = diff_projects(prev, [])
    self.assertEqual(changes, [{"id": "a", "kind": "deleted", "fields": [], "project": prev[0]}])

  def test_unchanged_emits_nothing(self) -> None:
    same = [{"id": "a", "name": "A", "color": "", "version": 1}]
    self.assertEqual(diff_projects(same, list(same)), [])

  def test_color_only_change_reports_color_and_version(self) -> None:
    prev = [{"id": "a", "name": "A", "color": "", "version": 1}]
    curr = [{"id": "a", "name": "A", "color": "#80868f", "version": 2}]
    changes = diff_projects(prev, curr)
    self.assertEqual(changes[0]["kind"], "updated")
    self.assertEqual(set(changes[0]["fields"]), {"color", "version"})

  def test_rename_reports_name(self) -> None:
    prev = [{"id": "a", "name": "A", "color": "#fff", "version": 1}]
    curr = [{"id": "a", "name": "Renamed", "color": "#fff", "version": 2}]
    changes = diff_projects(prev, curr)
    self.assertEqual(set(changes[0]["fields"]), {"name", "version"})

  def test_description_only_change_reports_description(self) -> None:
    # description is a watched field (new _WATCHED_FIELDS entry): a peer's edit surfaces it.
    prev = [{"id": "a", "name": "A", "color": "", "description": "", "version": 1}]
    curr = [{"id": "a", "name": "A", "color": "", "description": "now set", "version": 2}]
    changes = diff_projects(prev, curr)
    self.assertEqual(changes[0]["kind"], "updated")
    self.assertEqual(set(changes[0]["fields"]), {"description", "version"})

  def test_description_change_detected_when_field_absent_before(self) -> None:
    # A record missing the key defaults to "", so adding a description reports it even
    # when the version counter didn't move in the two snapshots we diffed.
    prev = [{"id": "a", "name": "A", "version": 1}]
    curr = [{"id": "a", "name": "A", "description": "x", "version": 1}]
    changes = diff_projects(prev, curr)
    self.assertEqual(set(changes[0]["fields"]), {"description"})


class WatchProjectsTest(unittest.TestCase):
  """poll_project_changes (one-shot) + watch_projects (blocking loop), driven by a
  monkeypatched list_projects so the suite stays network-free."""

  def test_poll_project_changes_one_shot(self) -> None:
    conn = ServerConnection("http://h:8090", token="t")
    snap = [{"id": "a", "name": "A", "color": "", "version": 1}]
    conn.list_projects = lambda: snap  # type: ignore[method-assign]
    current, changes = conn.poll_project_changes(previous=[])
    self.assertEqual(current, snap)
    self.assertEqual(changes[0]["kind"], "created")
    # Feeding the snapshot back reports no further change.
    _, changes2 = conn.poll_project_changes(previous=current)
    self.assertEqual(changes2, [])

  def test_watch_projects_seeds_baseline_then_fires(self) -> None:
    conn = ServerConnection("http://h:8090", token="t")
    seq = [
      [{"id": "a", "name": "A", "color": "", "version": 1}],       # baseline (silent)
      [{"id": "a", "name": "A", "color": "#000", "version": 2}],   # a colour change
    ]
    n = {"i": 0}

    def fetch() -> list:
      i = min(n["i"], len(seq) - 1)
      n["i"] += 1
      return seq[i]

    conn.list_projects = fetch  # type: ignore[method-assign]
    stop = threading.Event()
    received: list = list()

    def on_change(change) -> None:
      received.append(change)
      stop.set()  # end the loop after the first change

    conn.watch_projects(on_change, interval=0.01, stop=stop)
    self.assertEqual(len(received), 1)
    self.assertEqual(received[0]["kind"], "updated")
    self.assertEqual(set(received[0]["fields"]), {"color", "version"})


class RenameProjectTest(unittest.TestCase):
  def test_rename_reads_version_then_puts_name(self) -> None:
    conn = ServerConnection("http://h:8090", token="t")
    conn._current_version = lambda pid, fb: 7  # type: ignore[method-assign]
    captured: dict = dict()

    def stub_update(pid, layout=None, name=None, color=None, version=0):  # noqa: ANN001
      captured.update(pid=pid, name=name, version=version)
      return {"id": pid, "name": name, "version": version + 1}

    conn.update_project = stub_update  # type: ignore[method-assign]
    rec = conn.rename_project("p_1", "New Name")
    self.assertEqual(captured, {"pid": "p_1", "name": "New Name", "version": 7})
    self.assertEqual(rec["name"], "New Name")


if __name__ == "__main__":
  unittest.main()
