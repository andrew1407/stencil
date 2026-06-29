"""Tests for the layout dataclasses.

The canonical shape is mirrored from ``mcp/src/layout.rs`` and the browser's
``browser/js/core/layout.js`` (``buildLayoutPayload``): camelCase keys, a fixed
per-line field order with defaults, and a bare layout that serializes to ONLY
``{imageWidth, imageHeight, lines}`` (optional geometry/filter fields omitted
when absent).
"""

from __future__ import annotations

import importlib.util
import sys
import unittest
from pathlib import Path


# Load layout.py directly from its file path instead of `from pystencil.layout
# import ...`. Importing through the package would run pystencil/__init__.py,
# which eagerly imports sibling modules (core/image/...) added by other agents
# and not yet present; this keeps the layout tests self-contained.
_LAYOUT_PATH = Path(__file__).resolve().parents[1] / "pystencil" / "layout.py"
_spec = importlib.util.spec_from_file_location("pystencil_layout", _LAYOUT_PATH)
_layout = importlib.util.module_from_spec(_spec)
# Register before exec: @dataclass resolves the module's __dict__ from
# sys.modules[cls.__module__] when checking string annotations (PEP 563).
sys.modules[_spec.name] = _layout
_spec.loader.exec_module(_layout)
Layout = _layout.Layout
Line = _layout.Line
Point = _layout.Point


class LineSerializationTests(unittest.TestCase):
    def test_default_line_serializes_to_canonical_shape(self):
        """A Line built with defaults emits the exact camelCase contract."""
        line = Line(points=[Point(1.0, 2.0)])
        self.assertEqual(
            line.to_dict(),
            {
                "points": [{"x": 1.0, "y": 2.0}],
                "color": "#FFFF00",
                "thickness": 2.0,
                "markerSize": 4.0,
                "style": "solid",
                "locked": False,
                "fillColor": "transparent",
            },
        )

    def test_line_from_dict_applies_defaults_for_missing_keys(self):
        """Only ``points`` present -> every other field falls back to its default."""
        line = Line.from_dict({"points": [{"x": 5, "y": 6}]})
        self.assertEqual(line.color, "#FFFF00")
        self.assertEqual(line.thickness, 2.0)
        self.assertEqual(line.marker_size, 4.0)
        self.assertEqual(line.style, "solid")
        self.assertFalse(line.locked)
        self.assertEqual(line.fill_color, "transparent")
        self.assertEqual(line.points[0].x, 5.0)
        self.assertEqual(line.points[0].y, 6.0)

    def test_line_round_trip(self):
        """from_dict(to_dict()) preserves a fully customized line."""
        original = Line(
            points=[Point(0.0, 0.0), Point(10.0, 20.0)],
            color="#00FF00",
            thickness=3.5,
            marker_size=0.0,
            style="dashed",
            locked=True,
            fill_color="#112233",
        )
        self.assertEqual(Line.from_dict(original.to_dict()), original)


class LayoutSerializationTests(unittest.TestCase):
    def test_bare_layout_serializes_to_only_required_keys(self):
        """No optionals set -> exactly {imageWidth, imageHeight, lines}."""
        layout = Layout(image_width=640, image_height=480)
        self.assertEqual(
            layout.to_dict(),
            {"imageWidth": 640, "imageHeight": 480, "lines": []},
        )

    def test_optional_fields_added_only_when_present(self):
        """Setting filter/crop/rotation adds exactly those camelCase keys."""
        layout = Layout(
            image_width=100,
            image_height=200,
            image_filter="bw",
            crop_rect={"x": 1, "y": 2, "width": 3, "height": 4},
            rotation_quarters=1,
        )
        out = layout.to_dict()
        self.assertEqual(out["imageFilter"], "bw")
        self.assertEqual(out["cropRect"], {"x": 1, "y": 2, "width": 3, "height": 4})
        self.assertEqual(out["rotationQuarters"], 1)
        # filter_color stayed None, so its key must be absent.
        self.assertNotIn("filterColor", out)
        self.assertEqual(
            set(out.keys()),
            {"imageWidth", "imageHeight", "lines", "imageFilter", "cropRect", "rotationQuarters"},
        )

    def test_filter_color_key_present_when_set(self):
        """A custom duotone tint surfaces as the filterColor key."""
        layout = Layout(
            image_width=10,
            image_height=10,
            image_filter="custom",
            filter_color="#ABCDEF",
        )
        out = layout.to_dict()
        self.assertEqual(out["filterColor"], "#ABCDEF")

    def test_json_round_trip(self):
        """from_json(to_json()) preserves all data including optionals."""
        layout = Layout(
            image_width=320,
            image_height=240,
            lines=[Line(points=[Point(1.0, 1.0), Point(2.0, 2.0)], color="#FF0000")],
            image_filter="sepia",
            filter_color=None,
            crop_rect={"x": 0, "y": 0, "width": 50, "height": 50},
            rotation_quarters=2,
        )
        restored = Layout.from_json(layout.to_json())
        self.assertEqual(restored, layout)

    def test_from_dict_round_trip_preserves_data(self):
        """Layout.from_dict(L.to_dict()) preserves data (spec requirement)."""
        layout = Layout(
            image_width=8,
            image_height=8,
            lines=[Line(points=[Point(3.0, 4.0)])],
        )
        self.assertEqual(Layout.from_dict(layout.to_dict()), layout)

    def test_parse_missing_fields_yields_defaults(self):
        """A skeletal layout JSON parses with empty lines + None optionals."""
        layout = Layout.parse("{}")
        self.assertEqual(layout.image_width, 0)
        self.assertEqual(layout.image_height, 0)
        self.assertEqual(layout.lines, [])
        self.assertIsNone(layout.image_filter)
        self.assertIsNone(layout.filter_color)
        self.assertIsNone(layout.crop_rect)
        self.assertIsNone(layout.rotation_quarters)

    def test_parse_is_alias_for_from_json(self):
        """parse() and from_json() produce equal results."""
        text = '{"imageWidth": 5, "imageHeight": 6, "lines": []}'
        self.assertEqual(Layout.parse(text), Layout.from_json(text))

    def test_to_json_indent(self):
        """Pretty-printing produces multi-line JSON without changing data."""
        layout = Layout(image_width=1, image_height=1)
        pretty = layout.to_json(indent=2)
        self.assertIn("\n", pretty)
        self.assertEqual(Layout.from_json(pretty), layout)


if __name__ == "__main__":
    unittest.main()
