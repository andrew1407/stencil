"""How a supplied credential is classified, and the connections-listing filter."""

from __future__ import annotations

import unittest

from pystencil.server import ServerError, credential_filter_matches, parse_credential_filter
from tests.test_server_auth import CredentialRetentionTest

class CredentialKindTest(unittest.TestCase):
  """`credential_kind`: admin once the credential has PROVEN it can mint a
  session token, session once the probe accepted it directly, none when
  nothing was supplied (browser connectionManager credentialKind parity)."""

  _stubbed = CredentialRetentionTest._stubbed

  def test_admin_credential_is_recorded_at_connect(self) -> None:
    def handler(bearer, method, path):
      if path == "/auth/token":
        return {"token": "sess1"}
      if bearer != "sess1":
        raise ServerError("unauthorized", "bad token", status=401)
      return {"projects": []}

    conn, _ = self._stubbed("admintok", handler)
    self.assertEqual(conn.credential_kind, "")  # unclassified until connect()
    conn.connect()
    self.assertEqual(conn.credential_kind, "admin")

  def test_probe_accepted_token_is_a_session_credential(self) -> None:
    def handler(bearer, method, path):
      self.assertNotEqual(path, "/auth/token")  # never minted
      return {"projects": []}

    conn, _ = self._stubbed("sesstok", handler)
    conn.connect()
    self.assertEqual(conn.credential_kind, "session")

  def test_anonymous_mint_has_no_credential_to_classify(self) -> None:
    def handler(bearer, method, path):
      return {"token": "sess1"}

    conn, _ = self._stubbed(None, handler)
    conn.connect()
    self.assertEqual(conn.credential, "")
    self.assertEqual(conn.credential_kind, "none")

  def test_mid_session_re_mint_promotes_the_kind_to_admin(self) -> None:
    def handler(bearer, method, path):
      if path == "/auth/token":
        return {"token": "sess2"}
      if bearer != "sess2":
        raise ServerError("unauthorized", "expired", status=401)
      return {"projects": []}

    conn, _ = self._stubbed("admintok", handler)
    conn.credential_kind = "session"  # what an earlier probe had concluded
    conn.token = "stale"
    conn.list_projects()
    self.assertEqual(conn.credential_kind, "admin")

  def test_failed_re_mint_leaves_the_kind_alone(self) -> None:
    def handler(bearer, method, path):
      if path == "/auth/token":
        raise ServerError("forbidden", "mint refused", status=403)
      raise ServerError("unauthorized", "expired", status=401)

    conn, _ = self._stubbed("tok", handler)
    conn.credential_kind = "session"
    conn.token = "stale"
    with self.assertRaises(ServerError):
      conn.list_projects()
    self.assertEqual(conn.credential_kind, "session")  # nothing was proven


class CredentialFilterTest(unittest.TestCase):
  """The `/connections [admin|session]` listing filter (CLI parity)."""

  def test_parses_blank_all_admin_session_and_rejects_junk(self) -> None:
    self.assertEqual(parse_credential_filter(""), "all")
    self.assertEqual(parse_credential_filter("   "), "all")
    self.assertEqual(parse_credential_filter("ALL"), "all")
    self.assertEqual(parse_credential_filter("admin"), "admin")
    self.assertEqual(parse_credential_filter(" Session "), "session")
    self.assertIsNone(parse_credential_filter("bogus"))
    self.assertIsNone(parse_credential_filter("adminx"))

  def test_session_means_every_non_admin_credential(self) -> None:
    for kind in ("admin", "session", "none", ""):
      self.assertTrue(credential_filter_matches("all", kind))
    self.assertTrue(credential_filter_matches("admin", "admin"))
    for kind in ("session", "none", ""):
      self.assertFalse(credential_filter_matches("admin", kind))
      self.assertTrue(credential_filter_matches("session", kind))
    self.assertFalse(credential_filter_matches("session", "admin"))
