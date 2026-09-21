"""One prompt round through Editor.prompt: what it sends, and how often."""

from __future__ import annotations

import unittest

from tests.helpers.nativecase import NativeCase
import pystencil.llm as llm_module
from pystencil.editor import Editor
from pystencil.llm import LlmPlanError, MAX_ATTACHMENTS, execute_op_plan, parse_op_plan
from tests.helpers.stubs import _StubClient, _plan_json


class EditorPromptOfflineTest(unittest.TestCase):
  """Editor.prompt paths that need no native core (chat-only / execute=False)."""

  def test_chat_only_prompt_returns_no_outputs(self) -> None:
    client = _StubClient("Sounds good!")
    reply, outputs = Editor().prompt("just talk", llm=client)
    self.assertEqual(reply, "Sounds good!")
    self.assertEqual(outputs, [])
    # The single-turn message carried the prompt text.
    self.assertEqual(client.sent[0][0]["text"], "just talk")

  def test_execute_false_skips_execution(self) -> None:
    client = _StubClient(_plan_json(actions=[{"op": "rotate", "dir": "right"}]))
    editor = Editor()  # no image loaded — execution would raise, but is skipped
    reply, outputs = editor.prompt("rotate", llm=client, execute=False)
    self.assertEqual(reply, "ok")
    self.assertEqual(outputs, [])

  def test_invalid_plan_from_llm_raises(self) -> None:
    client = _StubClient(_plan_json(actions=[{"op": "rotate", "dir": "up"}]))
    with self.assertRaises(LlmPlanError):
      Editor().prompt("rotate", llm=client, execute=False)


class EditorPromptCapTests(NativeCase):
  """The single-turn Editor.prompt() path talks to the client directly, so it needs
  the §7 cap of its own — otherwise it is a way around Chat's."""

  class _Client:
    def chat(self, messages, system=None):
      return '{"version":1,"reply":"ok","actions":[],"variants":[]}'

  def test_over_the_cap_raises_before_anything_is_sent(self):
    ed = Editor().blank(20, 20)
    imgs = [("image/png", b"x")] * (MAX_ATTACHMENTS + 1)
    with self.assertRaises(ValueError) as cm:
      ed.prompt("edit it", images=imgs, llm=self._Client())
    self.assertIn("up to %d images" % MAX_ATTACHMENTS, str(cm.exception))

  def test_at_the_cap_still_goes_through(self):
    ed = Editor().blank(20, 20)
    imgs = [("image/png", b"x")] * MAX_ATTACHMENTS
    reply, _ = ed.prompt("edit it", images=imgs, llm=self._Client())
    self.assertEqual(reply, "ok")


class SingleModelRoundTest(NativeCase):
  """§3.0: a turn is ONE model round. A plan that draws a layout executes and the
  turn ends — nothing is sent afterwards and no note is appended to the reply."""

  @staticmethod
  def _layout_plan_json():
    return _plan_json(
      reply="outlined",
      actions=[
        {
          "op": "layout",
          "lines": [
            {
              "points": [{"x": 1, "y": 2}, {"x": 3, "y": 4}],
              "color": "#123456",
            }
          ],
        }
      ],
    )

  def test_a_layout_turn_makes_exactly_one_request(self):
    editor = Editor().blank(32, 48)
    # A second canned reply that must stay unsent.
    client = _StubClient(self._layout_plan_json(), _plan_json(reply="never sent"))
    reply, _outputs = editor.prompt("outline the box", llm=client)
    self.assertEqual(reply, "outlined")
    self.assertEqual(len(client.sent), 1)
    # The model's traced line IS the result — points and styling as planned.
    line = editor.layout().lines[-1]
    self.assertEqual([(p.x, p.y) for p in line.points], [(1, 2), (3, 4)])
    self.assertEqual(line.color, "#123456")

  def test_the_reply_carries_no_post_plan_note(self):
    editor = Editor().blank(32, 48)
    plan = parse_op_plan(self._layout_plan_json())
    execute_op_plan(plan, editor)
    self.assertEqual(plan.warnings, [])
    self.assertEqual(plan.reply, "outlined")

  def test_a_multi_image_layout_plan_warns_only_about_the_switch(self):
    editor = Editor().blank(32, 48)
    plan = parse_op_plan(
      _plan_json(
        reply="outlined",
        actions=[
          {"op": "image", "index": 1},
          {
            "op": "layout",
            "lines": [{"points": [{"x": 1, "y": 2}, {"x": 3, "y": 4}]}],
          },
        ],
      )
    )
    execute_op_plan(plan, editor)  # no attachments: only the switch is skipped
    self.assertEqual(len(plan.warnings), 1)
    self.assertIn("attached image 1", plan.warnings[0])
    for text in [plan.reply] + plan.warnings:
      self.assertNotIn("correction", text)
      self.assertNotIn("self-check", text)

  def test_the_module_exposes_no_post_plan_pass(self):
    for name in (
      "correct_layout",
      "LAYOUT_CORRECTION_PROMPT",
      "MULTI_IMAGE_SKIP_WARNING",
      "plan_is_multi_image",
    ):
      self.assertFalse(hasattr(llm_module, name), name)
