"""The script surface of the command line: the two REPL verbs and the three one-shot flags."""

from __future__ import annotations

import contextlib
import io
import json
import os
import unittest

from tests.clicase import _NativeReplCase, _PipelineCase

from pystencil import cli
from pystencil.cli.scriptplan import MAX_ACTIONS
from pystencil.editor import Editor


def _run(argv):
  """``cli.main(argv)`` with both channels captured: ``(code, stdout, stderr)``."""
  out, err = io.StringIO(), io.StringIO()
  with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
    code = cli.main(argv)
  return code, out.getvalue(), err.getvalue()


class ScriptConsoleTests(_NativeReplCase):
  """``/script`` and ``/script-run`` against the loaded image."""

  def _loaded(self):
    repl, out = self._repl(None)
    repl._editor = Editor().blank(400, 300)
    return repl, out

  def test_a_one_liner_edits_the_working_image(self):
    repl, out = self._loaded()
    repl.run(io.StringIO("/script @crop 10%;@filter bw\n"))
    self.assertEqual(repl._editor.image_size, (320, 240))
    self.assertIn("script -> 320x240", out.getvalue())

  def test_a_bare_verb_prints_its_usage(self):
    repl, out = self._loaded()
    repl.run(io.StringIO("/script\n/script-run\n"))
    self.assertIn("usage: /script <directives", out.getvalue())
    self.assertIn("usage: /script-run <file.stc>", out.getvalue())

  def test_an_error_reports_the_diagnostic_and_applies_nothing(self):
    repl, out = self._loaded()
    repl.run(io.StringIO("/script @crop 10%;@crp 5%\n"))
    self.assertIn("error: <console>:1:", out.getvalue())
    self.assertIn("[E_UNKNOWN_DIRECTIVE]", out.getvalue())
    self.assertEqual(repl._editor.image_size, (400, 300))

  def test_a_source_block_is_noted_and_the_ops_still_apply(self):
    repl, out = self._loaded()
    repl.run(io.StringIO("/script @source other.png:;@crop 10%\n"))
    self.assertIn("note: /script ignores @source", out.getvalue())
    self.assertEqual(repl._editor.image_size, (320, 240))

  def test_a_script_with_nothing_to_do_says_so(self):
    repl, out = self._loaded()
    repl.run(io.StringIO("/script @use px\n"))
    self.assertIn("that script recorded no edit here", out.getvalue())

  def test_a_script_needs_a_loaded_image(self):
    repl, out = self._repl(None)
    repl.run(io.StringIO("/script @crop 10%\n"))
    self.assertIn("error: no image loaded", out.getvalue())

  def test_script_run_reads_a_file_and_reports_a_missing_one(self):
    repl, out = self._loaded()
    with open("s.stc", "w", encoding="utf-8") as fh:
      fh.write("@crop 25%\n")
    repl.run(io.StringIO("/script-run s.stc\n/script-run nope.stc\n"))
    self.assertEqual(repl._editor.image_size, (200, 150))
    self.assertIn("error: could not read that script", out.getvalue())

  def test_a_save_reports_the_canonical_wrote_line(self):
    repl, out = self._loaded()
    repl.run(io.StringIO("/script @crop 25%;@save shot.png\n"))
    self.assertIn("wrote shot.png (200x150)", out.getvalue())
    self.assertTrue(os.path.exists("shot.png"))


class _ScriptFixture(_PipelineCase):
  """A temp working directory holding ``shots/`` with two 40x30 blanks."""

  def setUp(self):
    super().setUp()
    self._old = os.getcwd()
    os.chdir(self.tmp)
    self.addCleanup(lambda: os.chdir(self._old))
    os.mkdir("shots")
    for name in ("a.png", "b.png"):
      Editor().blank(40, 30).save(os.path.join("shots", name))

  def _write(self, name, body):
    with open(name, "w", encoding="utf-8") as fh:
      fh.write(body)
    return name


class ScriptOneShotTests(_ScriptFixture):
  """``--script`` and ``--script-check``."""

  def test_a_script_runs_over_every_input_a_source_names(self):
    self._write("s.stc", "@source shots/:\n    @crop 10%\n    @filter bw\n    @save\n")
    code, _, err = _run(["--script", "s.stc"])
    self.assertEqual(code, 0)
    self.assertIn("wrote shots/a-stencil.png (32x24)", err)
    self.assertIn("wrote shots/b-stencil.png (32x24)", err)

  def test_a_sourceless_script_edits_the_input_flag(self):
    self._write("s.stc", "@crop 25%\n@save out.png\n")
    code, _, err = _run(["--script", "s.stc", "-i", "shots/a.png"])
    self.assertEqual(code, 0)
    self.assertIn("wrote out.png (20x15)", err)

  def test_a_sourceless_script_without_an_input_fails(self):
    self._write("s.stc", "@crop 25%\n")
    code, _, err = _run(["--script", "s.stc"])
    self.assertEqual(code, 1)
    self.assertIn("error: this script has no @source block", err)

  def test_a_script_that_saves_nothing_says_so(self):
    self._write("s.stc", "@source shots/a.png:\n    @crop 10%\n")
    code, _, err = _run(["--script", "s.stc"])
    self.assertEqual(code, 0)
    self.assertIn("note: the script saved nothing", err)

  def test_an_error_stops_the_run_and_exits_one(self):
    self._write("s.stc", "@source shots/:\n    @crp 10%\n    @save\n")
    code, _, err = _run(["--script", "s.stc"])
    self.assertEqual(code, 1)
    self.assertIn("error: s.stc:2:5:", err)
    self.assertFalse(os.path.exists("shots/a-stencil.png"))

  def test_confine_output_refuses_a_save_outside_the_working_directory(self):
    self._write("s.stc", "@source shots/a.png:\n    @save /tmp/x.png\n")
    code, _, err = _run(["--script", "s.stc", "--confine-output"])
    self.assertEqual(code, 1)
    self.assertIn("error: refusing to save outside", err)

  def test_check_prints_the_grammar_an_editor_parses(self):
    self._write("s.stc", "@source a.png:\n  @crp 10%\n")
    code, out, _ = _run(["--script-check", "s.stc"])
    self.assertEqual(code, 1)
    self.assertTrue(out.startswith("s.stc:2:3: error: "))
    self.assertTrue(out.rstrip().endswith("[E_UNKNOWN_DIRECTIVE]"))

  def test_a_clean_script_checks_silently_and_a_warning_does_not_fail(self):
    self._write("ok.stc", "@source a.png:\n  @crop 10%\n  @save o.png\n")
    self.assertEqual(_run(["--script-check", "ok.stc"])[:2], (0, ""))
    self._write("warn.stc", "@source a.png:\n")
    code, out, _ = _run(["--script-check", "warn.stc"])
    self.assertEqual((code, "[W_EMPTY_BLOCK]" in out, "warning: " in out), (0, True, True))

  def test_only_one_script_flag_may_be_given(self):
    self._write("s.stc", "@source a.png:\n  @save o.png\n")
    code, _, err = _run(["--script", "s.stc", "--script-check", "s.stc"])
    self.assertEqual(code, 2)
    self.assertIn("error: pass only one of --script", err)


class ScriptPlanTests(_ScriptFixture):
  """The ``--script-plan`` envelope, parsed back."""

  def _plan(self, body):
    with open("p.stc", "w", encoding="utf-8") as fh:
      fh.write(body)
    code, out, _ = _run(["--script-plan", "p.stc"])
    return code, json.loads(out)

  def test_the_envelope_carries_the_version_label_and_one_block_per_source(self):
    code, plan = self._plan("@source shots/a.png:\n    @filter bw\n    @save\n")
    self.assertEqual((code, plan["version"], plan["script"], plan["diagnostics"]),
                     (0, 1, "p.stc", list()))
    block = plan["blocks"][0]
    self.assertEqual((block["sourceKind"], block["inputs"]), ("file", ["shots/a.png"]))
    self.assertEqual(block["dims"], {"width": 40, "height": 30})
    self.assertEqual(block["saves"],
                     [{"input": "shots/a.png", "path": "shots/a-stencil.png"}])

  def test_a_block_lowers_to_the_registrys_own_op_names(self):
    _, plan = self._plan(
      "@source shots/a.png:\n    @crop 10%\n    @rect (1,1) (5,5)\n    @save out.png\n")
    actions = plan["blocks"][0]["plans"][0]["actions"]
    self.assertEqual([a["op"] for a in actions], ["openFile", "crop", "layout", "save"])
    self.assertEqual(actions[1]["spec"]["x1"], "10%")
    self.assertTrue(actions[2]["lines"][0]["locked"])
    self.assertEqual(actions[3]["path"], "out.png")

  def test_a_custom_tint_is_written_as_hex_and_undo_carries_its_steps(self):
    _, plan = self._plan(
      "@source shots/a.png:\n    @filter aqua\n    @rect (1,1) (2,2)\n    @undo\n    @save o.png\n"
    )
    actions = plan["blocks"][0]["plans"][0]["actions"]
    tint = next(a for a in actions if a["op"] == "filter")
    self.assertEqual((tint["mode"], tint["tint"]), ("custom", "#00ffff"))
    self.assertEqual(next(a for a in actions if a["op"] == "undo")["steps"], 1)

  def test_a_url_block_has_no_dims_so_the_shape_ops_are_dropped(self):
    _, plan = self._plan(
      "@source https://e.example/a.png:\n    @line (0,0) (10%,10%)\n    @save\n"
    )
    block = plan["blocks"][0]
    self.assertIsNone(block["dims"])
    self.assertEqual([a["op"] for a in block["plans"][0]["actions"]],
                     ["openUrl", "save"])

  def test_an_error_reports_its_diagnostics_and_lowers_to_no_blocks(self):
    code, plan = self._plan("@source shots/a.png:\n  @crp 10%\n")
    self.assertEqual((code, plan["blocks"]), (1, list()))
    diag = plan["diagnostics"][0]
    self.assertEqual((diag["code"], diag["severity"]), ("E_UNKNOWN_DIRECTIVE", "error"))

  def test_the_actions_chunk_at_the_registry_limit(self):
    body = "@source shots/a.png:\n" + "    @filter bw\n" * (MAX_ACTIONS + 3)
    _, plan = self._plan(body)
    plans = plan["blocks"][0]["plans"]
    self.assertEqual([len(p["actions"]) for p in plans], [MAX_ACTIONS, 4])

  def test_max_actions_matches_the_canonical_op_registry(self):
    from pystencil.llm.plan import limits

    self.assertEqual(MAX_ACTIONS, limits.MAX_ACTIONS)


if __name__ == "__main__":
  unittest.main()
