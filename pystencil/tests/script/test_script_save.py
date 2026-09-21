"""Where a ``@save`` writes and in which codec, and the two doors that read a script."""

from __future__ import annotations

import contextlib
import io
import json
import os
import tempfile
import unittest

from tests.helpers.clicase import _NativeReplCase
from tests.helpers.nativecase import NativeCase

from pystencil import cli, codecs, scriptpaths
from pystencil.editor import Editor
from pystencil.script import ScriptError, parse_script


class SaveFormatTests(unittest.TestCase):
  """The one rule both the run and the plan name their output file by."""

  def test_a_writable_extension_survives_and_anything_else_becomes_png(self):
    self.assertEqual(scriptpaths.save_format("bmp"), "bmp")
    self.assertEqual(scriptpaths.save_format("PNG"), "png")
    self.assertEqual(scriptpaths.save_format("jpg"), "png")
    self.assertEqual(scriptpaths.save_format(None), "png")


class _SourcedCase(NativeCase):
  """A temp working directory holding one 40x30 blank as PNG and as BMP."""

  def setUp(self):
    self._dir = tempfile.TemporaryDirectory()
    self.addCleanup(self._dir.cleanup)
    self._old = os.getcwd()
    os.chdir(self._dir.name)
    self.addCleanup(lambda: os.chdir(self._old))
    for name in ("a.png", "a.bmp"):
      Editor().blank(40, 30).save(name)

  @staticmethod
  def _codec_of(path: str) -> str:
    with open(path, "rb") as handle:
      return codecs.sniff(handle.read(64))


class SaveCodecTests(_SourcedCase):
  """The bytes follow the target's extension, never the source's."""

  def test_a_bmp_target_on_a_png_source_writes_bmp_bytes(self):
    Editor().load("a.png").script("@save shot.bmp")
    self.assertEqual(self._codec_of("shot.bmp"), "bmp")

  def test_a_png_target_on_a_bmp_source_writes_png_bytes(self):
    Editor().load("a.bmp").script("@save shot.png")
    self.assertEqual(self._codec_of("shot.png"), "png")

  def test_an_extensionless_target_takes_the_sources_codec(self):
    Editor().load("a.bmp").script("@save final")
    self.assertEqual(self._codec_of("final.bmp"), "bmp")

  def test_a_bare_save_writes_png_beside_a_png_source(self):
    Editor().load("a.png").script("@save")
    self.assertEqual(self._codec_of("a-stencil.png"), "png")


class SaveTargetAgreementTests(_SourcedCase):
  """``--script-plan`` must name the file ``--script`` actually writes."""

  @staticmethod
  def _run(argv):
    out, err = io.StringIO(), io.StringIO()
    with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
      code = cli.main(argv)
    return code, out.getvalue(), err.getvalue()

  def _agreement_for(self, source: str) -> tuple:
    with open("s.stc", "w", encoding="utf-8") as handle:
      handle.write("@source %s:\n    @save final\n" % source)
    _, out, _ = self._run(["--script-plan", "s.stc"])
    planned = json.loads(out)["blocks"][0]["saves"][0]["path"]
    _, _, err = self._run(["--script", "s.stc"])
    return planned, err

  def test_the_plan_and_the_run_name_the_same_file_for_a_bmp_source(self):
    planned, err = self._agreement_for("a.bmp")
    self.assertEqual(planned, "final.bmp")
    self.assertIn("wrote final.bmp (40x30)", err)

  def test_the_plan_and_the_run_name_the_same_file_for_a_png_source(self):
    planned, err = self._agreement_for("a.png")
    self.assertEqual(planned, "final.png")
    self.assertIn("wrote final.png (40x30)", err)


class ScriptReadDoorTests(_SourcedCase):
  """``read_script`` is file-only, and every door that reads one goes through it."""

  def test_the_editor_door_refuses_a_traversal_path_like_the_cli_one(self):
    with self.assertRaises(ScriptError):
      Editor().load("a.png").script_run("../secret.stc")

  def test_a_stdin_dash_is_not_a_file_the_library_door_reads(self):
    with self.assertRaises(OSError):
      scriptpaths.read_script("-")

  def test_the_editor_door_runs_a_real_file(self):
    with open("s.stc", "w", encoding="utf-8") as handle:
      handle.write("@crop 50%\n")
    editor = Editor().load("a.png").script_run("s.stc")
    self.assertEqual(editor.applied, 1)


class ReplScriptRunTests(_NativeReplCase):
  """``/script-run`` never reads the stream the console itself is reading."""

  def test_a_stdin_dash_is_refused_without_ending_the_session(self):
    repl, out = self._repl(None)
    repl._editor = Editor().blank(400, 300)
    repl.run(io.StringIO("/script-run -\n/script @crop 25%\n"))
    self.assertIn("error: could not read that script", out.getvalue())
    self.assertEqual(repl._editor.image_size, (200, 150))


class LazyHandleReadTests(NativeCase):
  """``tokens`` and ``dump`` are read on first use, through the live handle."""

  def test_a_lazy_read_is_memoised_while_the_handle_lives(self):
    with parse_script("# hi\n@source a.png:\n    @crop 10%\n") as program:
      self.assertIn("directive", {t.kind for t in program.tokens})
      self.assertIs(program.tokens, program.tokens)
      self.assertIs(program.dump, program.dump)

  def test_a_closed_handle_refuses_a_lazy_read(self):
    program = parse_script("@source a.png:\n    @crop 10%\n")
    program.close()
    self.assertRaises(ScriptError, lambda: program.tokens)
    self.assertRaises(ScriptError, lambda: program.dump)

  def test_a_lazy_read_before_close_still_outlives_the_handle(self):
    with parse_script("@source a.png:\n    @crop 10%\n") as program:
      kept = program.dump
    self.assertIn("op crop", kept)


if __name__ == "__main__":
  unittest.main()
