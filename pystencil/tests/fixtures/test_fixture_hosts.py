"""The fetch guard over the shared SSRF host corpus (fixtures/net/hosts.json).

Each host is read out of a URL as ``_assert_fetchable`` reads it; the ``fetch`` variants are
judged by the guard itself, the ``serverTarget`` ones by the address table it carries.
"""

from __future__ import annotations

import ipaddress
import unittest
import urllib.parse

from tests.helpers.fixturebase import _FIXTURES, _load

from pystencil._net import _address_blocked, _assert_fetchable, _is_blocked_ip, _literal

_HOSTS = _load(_FIXTURES / "net" / "hosts.json")


def _url(host: str) -> str:
  """An unbracketed IPv6 host bracketed, and a zone ID's ``%`` written ``%25``, as a URL spells them."""
  bare = host.strip("[]")
  return "http://%s/x.png" % ("[%s]" % bare if ":" in bare else bare).replace("%", "%25")


def _verdict(blocked: bool) -> str:
  return "block" if blocked else "allow"


class HostCorpusTests(unittest.TestCase):
  def test_every_host_reads_and_judges_as_the_corpus_expects(self):
    for case in _HOSTS:
      with self.subTest(case=case["name"]):
        url = _url(case["host"])
        ip = _literal(urllib.parse.urlsplit(url).hostname)
        expect = case.get("expect")
        if expect is None:
          self.assertIsNone(ip, "a name is never read as an address")
          continue
        self.assertEqual(ip.packed, ipaddress.ip_address(case["address"]).packed)
        self.assertEqual(_verdict(_is_blocked_ip(ip, strict=True)), expect["fetch"])
        self.assertEqual(_verdict(_is_blocked_ip(ip, strict=False)), expect["fetch+allowLoopback"])
        self.assertEqual(_verdict(_address_blocked(ip, "serverTarget")), expect["serverTarget"])
        self.assertEqual(
          _verdict(_address_blocked(ip, "serverTarget", allowPrivate=True)),
          expect["serverTarget+allowPrivate"],
        )
        try:
          _assert_fetchable(url, strict=True)
          refused = False
        except ValueError:
          refused = True
        self.assertEqual(_verdict(refused), expect["fetch"], "a literal is judged without resolving")

  def test_every_host_reads_as_spelled_in_the_corpus(self):
    """``_literal`` reads the corpus spelling itself — brackets and zone IDs — as addr.zig does."""
    for case in _HOSTS:
      with self.subTest(case=case["name"]):
        ip = _literal(case["host"])
        if case.get("expect") is None:
          self.assertIsNone(ip)
        else:
          self.assertEqual(ip.packed, ipaddress.ip_address(case["address"]).packed)


if __name__ == "__main__":
  unittest.main()
