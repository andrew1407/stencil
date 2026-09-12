"""Editor.open_project over the shared .stencil vectors.

Part of the cross-surface fixture-conformance walk; the corpus roots and the
shared helpers live in :mod:`tests.fixturebase`.
"""

from __future__ import annotations

import base64
import json
import re
import struct
import unittest
import zlib

from tests.nativecase import require_core
from tests.fixturebase import _FIXTURES, _OVERRIDES, _filled_line_dict, _load, _norm

from pystencil.editor import Editor
from pystencil.layout import (
    DEFAULT_COLOR,
    DEFAULT_FILL_COLOR,
    DEFAULT_LOCKED,
    DEFAULT_POINT_SIZE,
    DEFAULT_STYLE,
    DEFAULT_THICKNESS,
)

_PROJECT_DIR = _FIXTURES / "fixtures" / "stencilProject"
# Parsed once per module, not once per test method.
_VALID = _load(_PROJECT_DIR / "valid.json")
_INVALID = _load(_PROJECT_DIR / "invalid.json")


def _png_1x1() -> bytes:
    def chunk(tag: bytes, data: bytes) -> bytes:
        return (
            struct.pack(">I", len(data)) + tag + data
            + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)
        )

    ihdr = struct.pack(">IIBBBBB", 1, 1, 8, 2, 0, 0, 0)
    idat = zlib.compress(b"\x00\xff\x00\x00")
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) + chunk(b"IDAT", idat) + chunk(b"IEND", b"")


_PNG_B64 = base64.b64encode(_png_1x1()).decode("ascii")


def _fill_line(sparse: dict) -> dict:
    d = {
        "points": sparse.get("points", []),
        "color": DEFAULT_COLOR,
        "thickness": DEFAULT_THICKNESS,
        "pointSize": DEFAULT_POINT_SIZE,
        "style": DEFAULT_STYLE,
        "locked": DEFAULT_LOCKED,
        "fillColor": DEFAULT_FILL_COLOR,
        "pointColor": "",
    }
    d.update({k: v for k, v in sparse.items() if k in d})
    return d


class TestStencilProjectFixtures(unittest.TestCase):
    def test_valid(self):
        require_core()  # this walk decodes the embedded PNG bytes
        # pystencil's reader decodes pixels (unlike the browser's format-only
        # parser), so the stub payload is swapped for a real 1x1 PNG; the file's
        # format-level content is untouched. Input is fed as bytes so a JSON
        # string is never mistaken for a path.
        overrides = _OVERRIDES["stencilProject"]
        for case in _VALID:
            with self.subTest(case=case["name"]):
                text = re.sub(r"base64,[A-Za-z0-9+/=]*", "base64," + _PNG_B64, case["file"])
                if overrides.get(case["name"], {}).get("verdict") == "error":
                    with self.assertRaises(ValueError):
                        Editor().open_project(text.encode("utf-8"))
                    continue
                ed = Editor().open_project(text.encode("utf-8"))
                raw = json.loads(case["file"])
                want = case["project"]
                # Name/color are kept verbatim (browser trims/normalizes).
                self.assertEqual(ed.name, raw.get("name") or "Untitled")
                self.assertEqual(ed.name.strip(), want["name"])
                self.assertEqual(ed.project_color, raw.get("color") or "")
                self.assertEqual(
                    ed.project_color.lstrip("#").lower(), want["color"].lstrip("#").lower()
                )
                self.assertEqual(ed.keywords, want["keywords"])
                self.assertEqual(ed._source or "", want["source"])
                self.assertEqual(ed._resource or "", want["resource"])
                # ext: the doc's value verbatim, lowercased (browser re-validates it).
                self.assertEqual(
                    ed._source_ext, ((raw.get("image") or {}).get("ext") or "png").lower()
                )
                # blank/blankColor/theme have no pystencil representation — skipped.
                snap = ed._current()
                lay = want["layout"]
                got_lines = [_filled_line_dict(ln) for ln in snap.lines]
                self.assertEqual(_norm(got_lines), _norm([_fill_line(x) for x in lay["lines"]]))
                self.assertEqual(snap.rotation, lay.get("rotationQuarters") or 0)
                self.assertEqual(snap.filter_mode, lay.get("imageFilter") or "")
                self.assertEqual(snap.filter_color, lay.get("filterColor") or "")
                self.assertEqual(ed._page_size, lay.get("pageSize") or "")
                cr = lay.get("cropRect")
                if cr is None:
                    self.assertIsNone(snap.crop)
                else:
                    # Read-both since Phase 6: canonical w/h wins, legacy
                    # width/height still reads — the corpus's w/h rects adopt.
                    self.assertEqual(
                        snap.crop,
                        (
                            cr.get("x", 0),
                            cr.get("y", 0),
                            cr.get("w", cr.get("width", 0)),
                            cr.get("h", cr.get("height", 0)),
                        ),
                    )

    def test_invalid(self):
        # Every corpus error case must fail here too; match on the CASE, not the
        # browser's message. dataUrl-non-string raises AttributeError (pinned).
        for case in _INVALID:
            with self.subTest(case=case["name"]):
                with self.assertRaises((ValueError, AttributeError)):
                    Editor().open_project(case["file"].encode("utf-8"))


if __name__ == "__main__":
    unittest.main()
