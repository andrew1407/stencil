"""The ``.stc`` handle, the path rules, the editor mixin and the whole-file block loop."""

from __future__ import annotations

import os
import tempfile
import unittest

from tests.helpers.nativecase import NativeCase

from pystencil import scriptpaths
from pystencil.editor import Editor
from pystencil.script import ScriptError, parse_script, read_script, run_script_text


class ScriptHandleTests(NativeCase):
  """What the core hands back across the ABI."""

  def test_a_parse_reads_blocks_ops_and_the_dump(self):
    with parse_script("@source a.png:\n    @crop 10%\n    @filter bw\n") as program:
      self.assertFalse(program.has_errors)
      self.assertEqual([b.kind for b in program.blocks], ["file"])
      self.assertEqual([op.kind for op in program.ops], ["open", "crop", "filter"])
      self.assertEqual(program.ops[1].toks, ("10%", "-10%", "10%", "-10%"))
      self.assertIn('op crop edit=1 "" [10% -10% 10% -10%] 0', program.dump)

  def test_a_diagnostic_carries_its_stable_code_and_position(self):
    with parse_script("@source a.png:\n  @crp 10%\n") as program:
      self.assertTrue(program.has_errors)
      diag = program.diagnostics[0]
      self.assertEqual((diag.severity, diag.code, diag.line, diag.col),
                       ("error", "E_UNKNOWN_DIRECTIVE", 2, 3))
      self.assertEqual(diag.format("a.stc"),
                       "a.stc:2:3: error: %s [E_UNKNOWN_DIRECTIVE]" % diag.message)

  def test_tokens_carry_the_editor_colouring_classes(self):
    with parse_script("# hi\n@source a.png:\n    @crop 10%\n") as program:
      kinds = {t.kind for t in program.tokens}
      self.assertIn("comment", kinds)
      self.assertIn("directive", kinds)

  def test_resolve_turns_length_tokens_into_pixels(self):
    with parse_script("@source a.png:\n    @crop 10%\n") as program:
      self.assertEqual(program.resolve(1, 100, 200), [10.0, 20.0, 80.0, 160.0])

  def test_a_closed_handle_refuses_to_resolve(self):
    program = parse_script("@source a.png:\n    @crop 10%\n")
    program.close()
    program.close()  # idempotent
    with self.assertRaises(ScriptError):
      program.resolve(1, 10, 10)

  def test_the_strings_outlive_the_handle(self):
    with parse_script("@source a.png:\n  @crp 1\n") as program:
      kept = (program.dump, program.diagnostics[0].message, program.blocks[0].source)
    self.assertEqual(kept[2], "a.png")
    self.assertTrue(kept[1])


class SaveTargetTests(unittest.TestCase):
  """The ``-stencil`` naming rule — twin of ``cli/src/script/save.zig``."""

  def test_a_bare_save_lands_beside_its_source_with_the_suffix(self):
    self.assertEqual(scriptpaths.resolve_target("", "shots/a.png", None, "png"),
                     "shots/a-stencil.png")

  def test_a_directory_target_keeps_the_suffix_and_a_named_one_does_not(self):
    self.assertEqual(scriptpaths.resolve_target("out/", "shots/a.png", None, "png"),
                     "out/a-stencil.png")
    self.assertEqual(scriptpaths.resolve_target("final", "shots/a.png", None, "bmp"),
                     "final.bmp")

  def test_a_target_with_a_known_extension_is_taken_verbatim(self):
    self.assertEqual(scriptpaths.resolve_target("out/exact.bmp", "a.png", None, "png"),
                     "out/exact.bmp")

  def test_a_video_frame_names_itself(self):
    self.assertEqual(scriptpaths.resolve_target("", "clip.mp4", 90, "png"),
                     "clip-frame-90-stencil.png")

  def test_a_url_source_still_yields_a_sane_local_name(self):
    self.assertEqual(
      scriptpaths.resolve_target("out/", "https://e.example/pics/a.png?x=1", None, "png"),
      "out/a-stencil.png",
    )

  def test_traversal_is_refused_always_and_outside_cwd_under_confinement(self):
    with self.assertRaises(ScriptError):
      scriptpaths.guard_target("../x.png", False)
    with self.assertRaises(ScriptError):
      scriptpaths.guard_target("/tmp/x.png", True)
    scriptpaths.guard_target("/tmp/x.png", False)
    scriptpaths.guard_target("out/x.png", True)


class SourceExpansionTests(unittest.TestCase):
  """What a ``@source`` spec names once the adapter opens it."""

  def setUp(self):
    self._dir = tempfile.TemporaryDirectory()
    self.addCleanup(self._dir.cleanup)
    for name in ("b.png", "a.png", "note.txt", ".hidden.png"):
      open(os.path.join(self._dir.name, name), "wb").close()

  def test_a_file_or_url_spec_expands_to_itself(self):
    self.assertEqual(scriptpaths.expand_source("https://e.example/a.png", "url"),
                     ["https://e.example/a.png"])
    self.assertEqual(scriptpaths.expand_source("a.png", "file"), ["a.png"])

  def test_a_foreign_scheme_is_refused_before_anything_opens_it(self):
    with self.assertRaises(ScriptError):
      scriptpaths.expand_source("ftp://h/a.png", "file")

  def test_a_directory_lists_sorted_media_only(self):
    found = scriptpaths.expand_source(self._dir.name + "/", "dir")
    self.assertEqual([os.path.basename(p) for p in found], ["a.png", "b.png"])

  def test_a_glob_matches_one_segment(self):
    found = scriptpaths.expand_source(self._dir.name + "/[ab].png", "glob")
    self.assertEqual([os.path.basename(p) for p in found], ["a.png", "b.png"])
    self.assertEqual(scriptpaths.expand_source(self._dir.name + "/c*.png", "glob"), list())

  def test_a_missing_directory_is_an_error(self):
    with self.assertRaises(ScriptError):
      scriptpaths.expand_source(self._dir.name + "/nope/", "dir")


class EditorScriptTests(NativeCase):
  """``Editor.script`` — ops straight onto the facade's own mutators."""

  def _blank(self):
    return Editor().blank(400, 300)

  def test_a_one_liner_crops_filters_and_draws(self):
    editor = self._blank()
    result = editor.script("@crop 10%; @filter bw; @line (0,0) (10,10)")
    self.assertEqual(result.applied, 3)
    self.assertEqual(editor.image_size, (320, 240))
    self.assertEqual(len(editor.layout().lines), 1)

  def test_an_error_applies_nothing(self):
    editor = self._blank()
    result = editor.script("@crop 10%\n@crp 5%\n")
    self.assertTrue(result.has_errors)
    self.assertEqual(result.applied, 0)
    self.assertEqual(editor.image_size, (400, 300))

  def test_a_sourced_block_is_reported_and_still_applies(self):
    editor = self._blank()
    result = editor.script("@source other.png:\n    @crop 10%\n")
    self.assertTrue(result.sources_ignored)
    self.assertEqual(editor.image_size, (320, 240))

  def test_undo_steps_the_editor_history(self):
    editor = self._blank()
    editor.script("@crop 10%\n@filter bw\n@undo\n")
    self.assertEqual(editor.image_size, (320, 240))
    self.assertIsNone(editor.layout().image_filter)

  def test_an_undo_a_redo_cancels_statically_before_anything_runs(self):
    editor = self._blank()
    editor.script("@crop 10%\n@filter bw\n@undo\n@redo\n")
    self.assertEqual(editor.layout().image_filter, "bw")

  def test_the_line_style_rides_from_use_line(self):
    editor = self._blank()
    editor.script("@use line red, dashed, 6\n@rect (10,10) (50,50)\n")
    line = editor.layout().lines[0]
    self.assertEqual((line.style, line.thickness, line.locked), ("dashed", 6.0, True))

  def test_frame_is_refused_because_there_is_no_video_decoder(self):
    editor = self._blank()
    with self.assertRaises(ScriptError):
      editor.script("@source clip.mp4:\n    @frame 3\n")

  def test_a_script_needs_a_loaded_image(self):
    with self.assertRaises(RuntimeError):
      Editor().script("@crop 10%")


class ScriptSaveTests(NativeCase):
  """``@save`` through the editor, and the whole-file block loop over a directory."""

  def setUp(self):
    self._dir = tempfile.TemporaryDirectory()
    self.addCleanup(self._dir.cleanup)
    self._old = os.getcwd()
    os.chdir(self._dir.name)
    self.addCleanup(lambda: os.chdir(self._old))
    os.mkdir("shots")
    for name in ("a.png", "b.png"):
      Editor().blank(40, 30).save(os.path.join("shots", name))

  def test_a_bare_save_writes_beside_the_source_and_reports(self):
    seen = list()
    run = run_script_text("@source shots/:\n    @crop 10%\n    @save\n", on_save=lambda *a: seen.append(a))
    self.assertEqual(run.saved, ["shots/a-stencil.png", "shots/b-stencil.png"])
    self.assertEqual([s[1:] for s in seen], [(32, 24), (32, 24)])
    self.assertTrue(os.path.exists("shots/a-stencil.png"))

  def test_a_sourceless_script_edits_the_given_input(self):
    run = run_script_text("@crop 50%\n@save out.png\n", source="shots/a.png")
    self.assertEqual(run.saved, ["out.png"])
    self.assertEqual(run.inputs, ["shots/a.png"])

  def test_a_sourceless_script_with_no_input_is_an_error(self):
    with self.assertRaises(ScriptError):
      run_script_text("@crop 50%\n")

  def test_an_empty_source_is_recorded_rather_than_raised(self):
    run = run_script_text("@source shots/none-*.png:\n    @save\n")
    self.assertEqual(run.empty_sources, ["shots/none-*.png"])
    self.assertEqual(run.saved, list())

  def test_confine_output_refuses_a_save_outside_the_working_directory(self):
    with self.assertRaises(ScriptError):
      run_script_text("@source shots/a.png:\n    @save /tmp/x.png\n", confine_output=True)

  def test_an_error_runs_nothing(self):
    run = run_script_text("@source shots/:\n    @crp 10%\n    @save\n")
    self.assertTrue(run.has_errors)
    self.assertEqual(run.saved, list())

  def test_read_script_refuses_a_traversal_path(self):
    with self.assertRaises(ScriptError):
      read_script("../secret.stc")


if __name__ == "__main__":
  unittest.main()
