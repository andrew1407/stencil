"""A collaboration server is judged as a ``serverTarget`` (pystencil/_net.py): a link-local,
metadata or reserved literal host is refused before any request is built, and a name resolving
to one before the first dial, so the bearer never reaches it, while a LAN or local server is not."""

from __future__ import annotations

import unittest
from unittest import mock

from pystencil.server import ServerConnection, ServerError
from pystencil.server import connection as server_connection


class ServerTargetGuardTests(unittest.TestCase):
  def test_metadata_and_link_local_hosts_raise_before_anything_is_sent(self) -> None:
    for url in ("http://169.254.169.254", "http://[fe80::1]:8090", "http://0.0.0.0:8090", "http://[::ffff:169.254.169.254]"):
      conn = ServerConnection(url, "tok-secret")
      with mock.patch.object(server_connection, "_http_open") as opened:
        with self.assertRaises(ServerError) as ctx:
          conn.list_projects()
        opened.assert_not_called()
      self.assertIn("refusing", str(ctx.exception))
      self.assertNotIn("tok-secret", str(ctx.exception))

  def test_a_lan_or_local_server_is_not_refused(self) -> None:
    for url in ("http://127.0.0.1:8090", "http://192.168.1.10:8090", "https://stencil.example"):
      conn = ServerConnection(url, "t")
      req = conn._build_request("GET", "/projects")
      self.assertTrue(req.full_url.startswith(conn.base))

  def test_a_name_that_resolves_to_the_metadata_address_raises_before_anything_is_sent(self) -> None:
    conn = ServerConnection("http://rebind.example:8090", "tok-secret")
    meta = [(2, 1, 6, "", ("169.254.169.254", 0))]
    with mock.patch("socket.getaddrinfo", return_value=meta) as looked, \
         mock.patch.object(server_connection, "_http_open") as opened:
      for _ in range(2):
        with self.assertRaises(ServerError) as ctx:
          conn.list_projects()
      opened.assert_not_called()
    self.assertEqual(looked.call_count, 1, "judged once per connection")
    self.assertIn("refusing", str(ctx.exception))
    self.assertNotIn("tok-secret", str(ctx.exception))

  def test_a_name_that_resolves_to_a_lan_address_is_dialled(self) -> None:
    conn = ServerConnection("http://nas.example:8090", "t")
    lan = [(2, 1, 6, "", ("192.168.1.10", 0))]
    with mock.patch("socket.getaddrinfo", return_value=lan), \
         mock.patch.object(server_connection, "_http_open", return_value=(200, b"[]")) as opened:
      conn.list_projects()
    opened.assert_called_once()


if __name__ == "__main__":
  unittest.main()
