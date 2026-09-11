"""Server URL handling: scheme/port normalization, loopback, invite links."""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

# Make the package importable when running `python3 -m unittest` from pystencil/.
_PKG_ROOT = Path(__file__).resolve().parent.parent
if str(_PKG_ROOT) not in sys.path:
    sys.path.insert(0, str(_PKG_ROOT))

from pystencil.server import (
    ServerConnection,
    is_loopback_host,
    normalize_url,
    split_invite_token,
)

class NormalizeUrlTest(unittest.TestCase):
    def test_secure_by_default_scheme(self) -> None:
        self.assertEqual(normalize_url("host:8090"), "https://host:8090")
        self.assertEqual(normalize_url("localhost:8090"), "http://localhost:8090")
        self.assertEqual(normalize_url("127.0.0.1:8090"), "http://127.0.0.1:8090")

    def test_is_loopback_host(self) -> None:
        self.assertTrue(is_loopback_host("localhost"))
        self.assertTrue(is_loopback_host("127.0.0.1"))
        self.assertTrue(is_loopback_host("::1"))
        self.assertFalse(is_loopback_host("example.com"))

    def test_strips_path_and_trailing_slash(self) -> None:
        self.assertEqual(normalize_url("http://host:8090/"), "http://host:8090")
        self.assertEqual(normalize_url("http://host:8090/projects/x"), "http://host:8090")

    def test_preserves_https_and_port(self) -> None:
        self.assertEqual(normalize_url("https://example.com:8443/api"), "https://example.com:8443")

    def test_trims_whitespace(self) -> None:
        self.assertEqual(normalize_url("  example.com  "), "https://example.com")

    def test_empty_raises(self) -> None:
        with self.assertRaises(ValueError):
            normalize_url("")
        with self.assertRaises(ValueError):
            normalize_url(None)


class SplitInviteTokenTest(unittest.TestCase):
    def test_fragment_parsed(self) -> None:
        self.assertEqual(
            split_invite_token("http://localhost:8090#token=abc123"),
            ("http://localhost:8090", "abc123"),
        )

    def test_explicit_token_wins(self) -> None:
        self.assertEqual(
            split_invite_token("http://localhost:8090#token=abc123", "explicit"),
            ("http://localhost:8090", "explicit"),
        )

    def test_fragmentless_url_unchanged(self) -> None:
        self.assertEqual(
            split_invite_token("https://host:8090", None),
            ("https://host:8090", None),
        )

    def test_empty_fragment_value_supplies_no_token(self) -> None:
        self.assertEqual(
            split_invite_token("http://localhost:8090#token="),
            ("http://localhost:8090", None),
        )

    def test_connection_populates_token_and_credential(self) -> None:
        conn = ServerConnection("http://host:8090#token=abc123")
        self.assertEqual(conn.base, "http://host:8090")
        self.assertEqual(conn.token, "abc123")
        self.assertEqual(conn.credential, "abc123")

    def test_connection_explicit_token_wins(self) -> None:
        conn = ServerConnection("http://host:8090#token=abc123", token="explicit")
        self.assertEqual(conn.base, "http://host:8090")
        self.assertEqual(conn.token, "explicit")
        self.assertEqual(conn.credential, "explicit")
