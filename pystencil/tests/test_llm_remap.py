"""Contract §1 executor-side coordinate re-mapping across crops and rotations."""

from __future__ import annotations

from tests.nativecase import NativeCase

from pystencil.editor import Editor
from pystencil.llm import execute_op_plan, parse_op_plan
from tests.stubs import _plan_json


class CoordinateRemapTest(NativeCase):
  """Contract §1 executor-side coordinate re-mapping against a REAL Editor:
  plan coordinates are in the pre-plan frame; the executor re-maps layout
  points through the plan's own crops/rotates and clamps them into bounds."""

  @staticmethod
  def _drawn_points(editor):
    return [(p.x, p.y) for p in editor.layout().lines[-1].points]

  def test_crop_translates_later_layout_points(self):
    editor = Editor().blank(200, 100)
    plan = parse_op_plan(
      _plan_json(
        actions=[
          {"op": "crop", "spec": {"x1": "100px"}},
          {
            "op": "layout",
            "lines": [
              {"points": [{"x": 150, "y": 50}, {"x": 160, "y": 60}]}
            ],
          },
        ]
      )
    )
    execute_op_plan(plan, editor)
    # The crop kept the right half (origin x=100): the model's (150, 50) —
    # in the frame it SAW — lands at (50, 50) of the cropped image.
    self.assertEqual(editor.image_size, (100, 100))
    self.assertEqual(self._drawn_points(editor), [(50.0, 50.0), (60.0, 60.0)])

  def test_rotate_maps_later_layout_points(self):
    # Clockwise quarter of a 40x30 view: (x, y) → (h − y, x).
    editor = Editor().blank(40, 30)
    plan = parse_op_plan(
      _plan_json(
        actions=[
          {"op": "rotate", "dir": "right"},
          {"op": "layout", "lines": [{"points": [{"x": 10, "y": 5}]}]},
        ]
      )
    )
    execute_op_plan(plan, editor)
    self.assertEqual(editor.image_size, (30, 40))
    self.assertEqual(self._drawn_points(editor), [(25.0, 10.0)])
    # Counter-clockwise: (x, y) → (y, w − x).
    editor = Editor().blank(40, 30)
    plan = parse_op_plan(
      _plan_json(
        actions=[
          {"op": "rotate", "dir": "left"},
          {"op": "layout", "lines": [{"points": [{"x": 10, "y": 5}]}]},
        ]
      )
    )
    execute_op_plan(plan, editor)
    self.assertEqual(self._drawn_points(editor), [(5.0, 30.0)])

  def test_quarter_turn_direction_matches_core_raster(self):
    # Validate the mapping's direction against the core's own pixel rotate: the mark must sit
    # where the continuous mapping (x, y) → (h − y, x) says its centre goes.
    from pystencil.core import get_core

    core = get_core()
    w, h = 3, 2
    data = bytearray(w * h * 4)
    data[(0 * w + 2) * 4] = 255  # mark pixel (2, 0)'s red channel
    out = core.rotate_image_rgba(bytes(data), w, h, 1)
    # Centre (2.5, 0.5) → (1.5, 2.5): pixel (1, 2) of the rotated 2x3 image.
    new_w = h
    self.assertEqual(out[(2 * new_w + 1) * 4], 255)

  def test_crop_then_rotate_compose(self):
    editor = Editor().blank(200, 120)
    plan = parse_op_plan(
      _plan_json(
        actions=[
          {"op": "crop", "spec": {"x1": "100px"}},
          {"op": "rotate", "dir": "right"},
          {"op": "layout", "lines": [{"points": [{"x": 150, "y": 20}]}]},
        ]
      )
    )
    execute_op_plan(plan, editor)
    # (150, 20) − crop origin (100, 0) → (50, 20); clockwise quarter of the
    # 100x120 cropped view → (120 − 20, 50) = (100, 50).
    self.assertEqual(editor.image_size, (120, 100))
    self.assertEqual(self._drawn_points(editor), [(100.0, 50.0)])

  def test_layout_points_clamped_without_crop_or_rotate(self):
    editor = Editor().blank(20, 20)
    plan = parse_op_plan(
      _plan_json(
        actions=[
          {
            "op": "layout",
            "lines": [{"points": [{"x": -5, "y": 30}, {"x": 500, "y": 3}]}],
          }
        ]
      )
    )
    execute_op_plan(plan, editor)
    self.assertEqual(self._drawn_points(editor), [(0.0, 20.0), (20.0, 3.0)])

  def test_variant_layout_remaps_through_top_level_crop(self):
    editor = Editor().blank(200, 100)  # white
    plan = parse_op_plan(
      _plan_json(
        actions=[{"op": "crop", "spec": {"x1": "100px"}}],
        variants=[
          {
            "label": "marked",
            "actions": [
              {
                "op": "layout",
                "lines": [
                  {
                    "points": [
                      {"x": 110, "y": 10},
                      {"x": 190, "y": 90},
                    ],
                    "color": "#ff0000",
                    "thickness": 5,
                  }
                ],
              }
            ],
          }
        ],
      )
    )
    outputs = execute_op_plan(plan, editor)
    self.assertEqual(len(outputs), 2)
    variant = outputs[1]
    self.assertEqual((variant.width, variant.height), (100, 100))
    # The pre-plan diagonal (110,10)-(190,90) re-maps through the top-level
    # crop to (10,10)-(90,90) and passes through the variant's centre.
    d = (50 * variant.width + 50) * 4
    self.assertGreater(variant.data[d], 200)  # red
    self.assertLess(variant.data[d + 1], 80)  # not white any more
