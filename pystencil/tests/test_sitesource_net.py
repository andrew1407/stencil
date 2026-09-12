"""The scraper's SSRF guard (parity with the Zig CLI's net.zig)."""

from __future__ import annotations

import ipaddress
import unittest

from pystencil._net import _assert_fetchable, _is_blocked_ip
from pystencil.sitesource import _sub_strict

class SsrfGuardTests(unittest.TestCase):
    def _blocked(self, host, strict):
        return _is_blocked_ip(ipaddress.ip_address(host), strict)

    def test_internal_ranges_blocked_in_both_modes(self):
        for host in (
            "169.254.169.254",  # cloud metadata
            "10.0.0.5",
            "172.16.4.4",
            "192.168.1.1",
            "100.64.0.1",  # CGNAT
            "0.0.0.0",
            "fe80::1",  # link-local
            "fc00::1",  # ULA
            "::ffff:169.254.169.254",  # IPv4-mapped metadata
            "::ffff:10.0.0.1",  # IPv4-mapped private
        ):
            self.assertTrue(self._blocked(host, False), host)
            self.assertTrue(self._blocked(host, True), host)

    def test_loopback_allowed_only_when_not_strict(self):
        for host in ("127.0.0.1", "127.9.9.9", "::1", "::ffff:127.0.0.1"):
            self.assertFalse(self._blocked(host, False), host)  # user-named URL
            self.assertTrue(self._blocked(host, True), host)  # harvested sub-resource

    def test_public_hosts_allowed(self):
        for host in ("8.8.8.8", "93.184.216.34", "2606:2800:220:1:248:1893:25c8:1946"):
            self.assertFalse(self._blocked(host, False), host)
            self.assertFalse(self._blocked(host, True), host)

    def test_assert_fetchable_rejects_ip_literals_and_localhost(self):
        for url in (
            "http://169.254.169.254/latest/meta-data/",
            "http://10.0.0.1/x",
            "http://[::1]/x",  # strict → loopback blocked
        ):
            with self.assertRaises(ValueError):
                _assert_fetchable(url, strict=True)
        with self.assertRaises(ValueError):
            _assert_fetchable("http://localhost/x", strict=True)
        # Non-strict tolerates loopback for a user-named URL.
        _assert_fetchable("http://127.0.0.1:8080/x", strict=False)
        _assert_fetchable("http://localhost/x", strict=False)

    def test_assert_fetchable_catches_alternate_numeric_encodings(self):
        # getaddrinfo canonicalizes these to 127.0.0.1 → blocked in strict mode.
        for url in ("http://2130706433/x", "http://0x7f000001/x"):
            with self.assertRaises(ValueError):
                _assert_fetchable(url, strict=True)

    def test_sub_strict_same_host_exception(self):
        # Same host as the page → loopback tolerated (own localhost gallery).
        self.assertFalse(_sub_strict("http://127.0.0.1:8080/a.png", "127.0.0.1"))
        self.assertFalse(_sub_strict("http://cdn.example.com/x.png", "cdn.example.com"))
        # Different host → strict (the SSRF pivot).
        self.assertTrue(_sub_strict("http://127.0.0.1/admin", "evil.com"))
        self.assertTrue(_sub_strict("http://169.254.169.254/meta", "site.com"))
