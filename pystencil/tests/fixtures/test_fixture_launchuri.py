"""The launch-URI parser over the shared corpus (fixtures/deepLink/launchQuery.json).

Every vector's URL parts parse to its expectation, the fields it leaves out being the empty
query's; the whole-URL entry splits a link into the same parts.
"""

from __future__ import annotations

import unittest

from tests.helpers.fixturebase import _FIXTURES, _load

from pystencil.cli.launch import uri as _launchuri

_VECTORS = _load(_FIXTURES / "deepLink" / "launchQuery.json")


class LaunchUriCorpusTests(unittest.TestCase):
  def test_every_vector_parses_to_its_expectation(self):
    self.assertGreaterEqual(len(_VECTORS), 20)
    for case in _VECTORS:
      with self.subTest(case=case["name"]):
        got = _launchuri.parse_parts(case.get("search", ""), case.get("hash", ""))
        want = None if case["expect"] is None else {**_launchuri.empty_query(), **case["expect"]}
        self.assertEqual(got, want)

  def test_a_whole_url_splits_into_the_same_parts(self):
    q = _launchuri.parse("https://app.example/?theme=dark#connect=https://srv.example&token=t")
    self.assertEqual(q["theme"], "dark")
    self.assertEqual(q["connect"], [{"url": "https://srv.example", "token": "t"}])
    scheme = _launchuri.parse("stencil://open?src=https%3A%2F%2Fx%2Fa.png&frame=3")
    self.assertEqual(scheme["picture"], {"kind": "source", "url": "https://x/a.png", "frame": 3})
    self.assertIsNone(_launchuri.parse("file:///tmp/x?source=y"))
    self.assertIsNone(_launchuri.parse("https://cdn.example/a.png?width=800"), "a picture URL is not a link")
    self.assertIsNone(_launchuri.parse("photo.png"))
