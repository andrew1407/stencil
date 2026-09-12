"""Credential retention and the mid-session session-token re-mint."""

from __future__ import annotations

import unittest

from pystencil.server import ServerConnection, ServerError

class CredentialRetentionTest(unittest.TestCase):
  """Credential retention + mid-session re-mint (port of extension
  connections.js req()/connect(): the user-supplied value outlives the
  session token, so a server restart/DB wipe is recovered in place)."""

  def _stubbed(self, token, handler, url="http://host:8090"):
    # A connection whose _open is replaced by `handler(bearer, method, path)`;
    # returns (conn, calls) with each call recorded as (bearer, method, path).
    conn = ServerConnection(url, token=token)
    calls = []

    def stub_open(req, raw=False):
      bearer = (req.get_header("Authorization") or "").removeprefix("Bearer ")
      path = req.full_url[len("http://host:8090"):]
      calls.append((bearer, req.get_method(), path))
      return handler(bearer, req.get_method(), path)

    conn._open = stub_open
    return conn, calls

  def test_connect_keeps_credential_and_admin_mints(self) -> None:
    # The supplied value is the ADMIN token: probe fails 401, mint succeeds,
    # and the original value is retained as `credential`.
    def handler(bearer, method, path):
      if path == "/auth/token":
        return {"token": "sess1"}
      if bearer != "sess1":
        raise ServerError("unauthorized", "bad token", status=401)
      return {"projects": []}

    conn, calls = self._stubbed("admintok", handler)
    conn.connect()
    self.assertEqual(conn.status, "connected")
    self.assertEqual(conn.token, "sess1")
    self.assertEqual(conn.credential, "admintok")
    self.assertEqual(calls, [
      ("admintok", "GET", "/projects"),
      ("admintok", "POST", "/auth/token"),
      ("sess1", "GET", "/projects"),
    ])

  def test_invite_fragment_token_feeds_credential_mint(self) -> None:
    # An invite link's fragment token is the ADMIN credential: probe 401s,
    # the mint uses it, and it is retained as `credential` for re-mints.
    def handler(bearer, method, path):
      if path == "/auth/token":
        return {"token": "sess1"}
      if bearer != "sess1":
        raise ServerError("unauthorized", "bad token", status=401)
      return {"projects": []}

    conn, calls = self._stubbed(None, handler, url="http://host:8090#token=admintok")
    conn.connect()
    self.assertEqual(conn.status, "connected")
    self.assertEqual(conn.token, "sess1")
    self.assertEqual(conn.credential, "admintok")
    self.assertEqual(calls, [
      ("admintok", "GET", "/projects"),
      ("admintok", "POST", "/auth/token"),
      ("sess1", "GET", "/projects"),
    ])

  def test_connect_probe_500_propagates_without_minting(self) -> None:
    def handler(bearer, method, path):
      raise ServerError("internal", "boom", status=500)

    conn, calls = self._stubbed("tok", handler)
    with self.assertRaises(ServerError) as cm:
      conn.connect()
    self.assertEqual(cm.exception.status, 500)
    self.assertEqual(conn.status, "error")
    self.assertEqual(calls, [("tok", "GET", "/projects")])  # no mint attempt

  def test_connect_statusless_error_still_falls_back_to_mint(self) -> None:
    # A ServerError without an HTTP status keeps the pre-existing behavior
    # (mint fallback) rather than guessing it was a 500.
    def handler(bearer, method, path):
      if path == "/auth/token":
        return {"token": "sess1"}
      if bearer != "sess1":
        raise ServerError("", "opaque failure", status=None)
      return {"projects": []}

    conn, _ = self._stubbed("tok", handler)
    conn.connect()
    self.assertEqual(conn.token, "sess1")

  def test_stale_session_re_mints_once_and_retries(self) -> None:
    # Mid-session 401 (server restarted): one re-mint with the retained
    # credential, then the original request is retried in place.
    def handler(bearer, method, path):
      if path == "/auth/token":
        self.assertEqual(bearer, "admintok")
        return {"token": "sess2"}
      if bearer != "sess2":
        raise ServerError("unauthorized", "expired", status=401)
      return {"projects": [{"id": "p1"}]}

    conn, calls = self._stubbed("admintok", handler)
    conn.token = "stale"  # session token from before the wipe
    self.assertEqual(conn.list_projects(), [{"id": "p1"}])
    self.assertEqual(conn.token, "sess2")
    self.assertEqual(calls, [
      ("stale", "GET", "/projects"),
      ("admintok", "POST", "/auth/token"),
      ("sess2", "GET", "/projects"),
    ])

  def test_stale_session_without_credential_propagates(self) -> None:
    def handler(bearer, method, path):
      raise ServerError("unauthorized", "expired", status=401)

    conn, calls = self._stubbed(None, handler)  # nothing supplied
    conn.token = "stale"
    with self.assertRaises(ServerError):
      conn.list_projects()
    self.assertEqual(len(calls), 1)  # no mint, no retry

  def test_failed_re_mint_surfaces_original_error_no_loop(self) -> None:
    # The re-mint itself failing must surface the ORIGINAL 401, after
    # exactly one mint attempt (no retry loops on /auth/token).
    def handler(bearer, method, path):
      if path == "/auth/token":
        raise ServerError("forbidden", "mint refused", status=403)
      raise ServerError("unauthorized", "expired", status=401)

    conn, calls = self._stubbed("admintok", handler)
    conn.token = "stale"
    with self.assertRaises(ServerError) as cm:
      conn.list_projects()
    self.assertEqual(cm.exception.status, 401)
    self.assertEqual(cm.exception.message, "expired")
    self.assertEqual([c[2] for c in calls], ["/projects", "/auth/token"])

  def test_non_auth_error_does_not_re_mint(self) -> None:
    def handler(bearer, method, path):
      raise ServerError("notFound", "gone", status=404)

    conn, calls = self._stubbed("admintok", handler)
    with self.assertRaises(ServerError):
      conn.get_project("p1")
    self.assertEqual(len(calls), 1)
