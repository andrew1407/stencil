"""``codecs.image_dimensions`` over the shared image-header corpus
(fixtures/imageHeader/cases.json). The read reports a size and no format, so the size alone
is compared; a divergence would be pinned under ``imageHeader`` in fixture_overrides.json.
"""

from __future__ import annotations

import base64
import unittest

from tests.helpers.fixturebase import _FIXTURES, _OVERRIDES, _load

from pystencil.codecs import image_dimensions

_CASES = _load(_FIXTURES / "fixtures" / "imageHeader" / "cases.json")


class ImageHeaderCorpusTests(unittest.TestCase):
  def test_every_header_measures_as_the_corpus_expects(self):
    overrides = _OVERRIDES.get("imageHeader", {})
    for case in _CASES:
      with self.subTest(case=case["name"]):
        expect = overrides.get(case["name"], case)["expect"]
        want = None if expect is None else (expect["width"], expect["height"])
        self.assertEqual(image_dimensions(base64.b64decode(case["base64"])), want)


if __name__ == "__main__":
  unittest.main()
