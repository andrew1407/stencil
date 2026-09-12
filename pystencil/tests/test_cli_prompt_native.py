"""/prompt round-trips that execute a real plan on the session's editor: top-level actions,
variant files, the §1 layout re-map through the plan's own crop, and §2.1 `save`.
"""

from __future__ import annotations

import io

from pystencil import cli
from pystencil import codecs
from pystencil.editor import Editor
from pystencil.llm import LlmConfig

from tests.clicase import _MockLlmClient, _NativeReplCase


class ReplPromptNativeTest(_NativeReplCase):
  """/prompt round-trips executing a real plan on the session's editor."""

  def test_prompt_executes_plan_and_writes_variants(self) -> None:
    plan = (
      '{"version":1,"reply":"rotated; one variant",'
      '"actions":[{"op":"rotate","dir":"right"}],'
      '"variants":[{"label":"B&W One!","actions":[{"op":"filter","mode":"bw"}]}]}'
    )
    client = _MockLlmClient(plan)
    out = io.StringIO()
    repl = cli._Repl(out)
    repl._llm = LlmConfig()
    repl._llm_client = lambda: client
    repl.run(io.StringIO("/blank 32 48\n/prompt rotate it and add a b&w variant\n"))
    text = out.getvalue()
    self.assertIn("rotated; one variant", text)
    self.assertIn("applied 1 action(s) -> 48x32", text)
    # The current image rode along base64-able as a PNG attachment.
    media_type, data = client.sent[0][0]["images"][0]
    self.assertEqual(media_type, "image/png")
    self.assertEqual(codecs.decode(bytes(data))[:2], (32, 48))
    # The session editor was mutated in place by the top-level actions.
    self.assertEqual(repl._editor.image_size, (48, 32))
    # The variant landed as variant-<sanitized-label>.png with the wrote line.
    self.assertIn("wrote variant-b-w-one.png (48x32)", text)
    with open("variant-b-w-one.png", "rb") as fh:
      w, h, _pixels = codecs.decode(fh.read())
    self.assertEqual((w, h), (48, 32))

  def test_misplaced_variant_op_drops_the_variant_not_the_turn(self) -> None:
    # §1: a `clear` inside a variant costs THAT variant only — the top-level
    # actions and the well-formed variants still run, with a warning.
    plan = (
      '{"version":1,"reply":"rotated; two variants",'
      '"actions":[{"op":"rotate","dir":"right"}],'
      '"variants":[{"label":"wiped","actions":[{"op":"clear"}]},'
      '{"label":"grey","actions":[{"op":"filter","mode":"bw"}]}]}'
    )
    out = io.StringIO()
    repl = cli._Repl(out)
    repl._llm = LlmConfig()
    repl._llm_client = lambda: _MockLlmClient(plan)
    repl.run(io.StringIO("/blank 32 48\n/prompt rotate it, wipe it, grey it\n"))
    text = out.getvalue()
    self.assertNotIn("error:", text)
    self.assertIn('[warning] dropped variant 1 ("wiped") — the "clear" op adjusts '
           "the console, not the image, and cannot appear in a variant", text)
    self.assertIn("applied 1 action(s) -> 48x32", text)
    self.assertIn("wrote variant-grey.png (48x32)", text)
    self.assertTrue(repl._editor.has_image())  # the image survived

  def test_a_plan_of_only_a_bad_variant_is_a_reply_plus_warning(self) -> None:
    plan = (
      '{"version":1,"reply":"here you go","actions":[],'
      '"variants":[{"label":"wiped","actions":[{"op":"clear"}]}]}'
    )
    out = io.StringIO()
    repl = cli._Repl(out)
    repl._llm = LlmConfig()
    repl._llm_client = lambda: _MockLlmClient(plan)
    repl.run(io.StringIO("/blank 32 48\n/prompt wipe it in a variant\n"))
    text = out.getvalue()
    self.assertNotIn("error:", text)
    self.assertIn("here you go", text)
    self.assertIn('[warning] dropped variant 1 ("wiped")', text)
    self.assertTrue(repl._editor.has_image())

  def test_prompt_remaps_layout_through_plan_crop(self) -> None:
    # Contract §1: /prompt plans arrive in the pre-plan frame; the executor
    # re-maps the layout through the plan's own crop before drawing.
    plan = (
      '{"version":1,"reply":"cropped and lined",'
      '"actions":[{"op":"crop","spec":{"x1":"100px"}},'
      '{"op":"layout","lines":[{"points":[{"x":150,"y":50},{"x":160,"y":60}]}]}]}'
    )
    client = _MockLlmClient(replies=[plan])
    repl, _out = self._repl(client)
    repl.run(io.StringIO("/blank 200 100\n/prompt crop then draw\n"))
    self.assertEqual(repl._editor.image_size, (100, 100))
    points = [(p.x, p.y) for p in repl._editor.layout().lines[-1].points]
    self.assertEqual(points, [(50.0, 50.0), (60.0, 60.0)])

  def test_multi_image_plan_saves_a_project(self) -> None:
    # §2.1: `save` writes <name>.stencil beside the output. The console attaches
    # no images of its own, so the switch itself is skipped with a per-action
    # note — the rest of the plan still runs, in the turn's ONE model round.
    plan = (
      '{"version":1,"reply":"kept it",'
      '"actions":[{"op":"image","index":1},'
      '{"op":"layout","lines":[{"points":[{"x":1,"y":2},{"x":3,"y":4}]}]},'
      '{"op":"save","name":"kept"}]}'
    )
    client = _MockLlmClient(plan)
    repl, out = self._repl(client)
    repl.run(io.StringIO("/blank 32 48\n/prompt outline and keep it\n"))
    text = out.getvalue()
    self.assertEqual(len(client.sent), 1)
    self.assertNotIn("correction", text)
    self.assertIn("[warning] Skipped switching to attached image 1", text)
    self.assertIn("saved project kept.stencil", text)
    reopened = Editor().open_project("kept.stencil")
    self.assertEqual(reopened.image_size, (32, 48))

  _LAYOUT_PLAN = (
    '{"version":1,"reply":"outlined",'
    '"actions":[{"op":"layout","lines":[{"points":[{"x":1,"y":2},{"x":3,"y":4}],'
    '"color":"#123456"}]}]}'
  )

  def test_layout_plan_is_exactly_one_model_round(self) -> None:
    # §3.0: the plan executes, the reply is shown, the turn ends — the second
    # canned reply proves no further request goes out.
    client = _MockLlmClient(
      replies=[self._LAYOUT_PLAN, '{"version":1,"reply":"never sent"}']
    )
    repl, out = self._repl(client)
    repl.run(io.StringIO("/blank 32 48\n/prompt outline the box\n"))
    self.assertEqual(len(client.sent), 1)
    self.assertEqual(len(client.replies), 1)  # the extra reply stayed unsent
    text = out.getvalue()
    self.assertIn("outlined", text)
    self.assertNotIn("[warning]", text)
    # The traced line stands exactly as planned, styling kept.
    line = repl._editor.layout().lines[-1]
    self.assertEqual([(p.x, p.y) for p in line.points], [(1, 2), (3, 4)])
    self.assertEqual(line.color, "#123456")

  def test_a_layout_turn_prints_no_post_plan_note(self) -> None:
    client = _MockLlmClient(replies=[self._LAYOUT_PLAN])
    repl, out = self._repl(client)
    repl.run(io.StringIO("/blank 32 48\n/prompt outline the box\n"))
    text = out.getvalue().lower()
    for phrase in ("correction", "self-check", "checking the outlines", "sharpen"):
      self.assertNotIn(phrase, text)
