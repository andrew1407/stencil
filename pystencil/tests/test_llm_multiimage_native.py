"""§2.1 multi-image ops against a REAL Editor; self-skips without a C++ compiler."""

from __future__ import annotations

import os
import shutil
import tempfile
import unittest

from pystencil.editor import Editor
from pystencil.llm import execute_op_plan, parse_op_plan
from tests.stubs import _plan_json


class MultiImageNativeTest(unittest.TestCase):
    """The `image` op against a REAL Editor: the attachment becomes the working image
    and the §1 coordinate re-mapping resets with it (ExecuteOpPlanNativeTest pattern)."""

    @classmethod
    def setUpClass(cls) -> None:
        try:
            from pystencil.core import get_core

            get_core()
        except Exception as exc:  # noqa: BLE001 - any failure means "no native lib"
            raise unittest.SkipTest("native core unavailable: %s" % exc)

    def setUp(self) -> None:
        self.tmp = tempfile.mkdtemp(prefix="stencil_save_native_")
        self.addCleanup(shutil.rmtree, self.tmp, True)
        self.attachment = (
            "image/png",
            Editor().blank(40, 30, color="#204080").result().encode("png"),
            "beach photo.png",
        )

    def test_the_attachment_replaces_the_working_image_and_resets_the_frame(self) -> None:
        editor = Editor().blank(100, 100)
        plan = parse_op_plan(
            _plan_json(
                actions=[
                    {"op": "crop", "spec": {"x1": "50%"}},
                    {"op": "image", "index": 1},
                    {"op": "layout", "lines": [{"points": [{"x": 5, "y": 7}]}]},
                ]
            )
        )
        execute_op_plan(plan, editor, [self.attachment])
        self.assertEqual(editor.image_size, (40, 30))  # the attachment, not the crop
        # The layout after the switch lands as written: the crop's translation is gone.
        pts = editor.layout().lines[-1].points
        self.assertEqual([(p.x, p.y) for p in pts], [(5.0, 7.0)])

    def test_save_writes_a_loadable_stencil_named_after_the_attachment(self) -> None:
        editor = Editor().blank(20, 20)
        plan = parse_op_plan(
            _plan_json(actions=[{"op": "image", "index": 1}, {"op": "save"}])
        )
        execute_op_plan(plan, editor, [self.attachment], self.tmp)
        path = os.path.join(self.tmp, "beach photo.stencil")
        self.assertEqual(plan.saved, [path])
        reopened = Editor().open_project(path)
        self.assertEqual(reopened.image_size, (40, 30))
