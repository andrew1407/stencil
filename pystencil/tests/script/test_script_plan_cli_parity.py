"""``--script-plan`` against the built Zig CLI's: the WHOLE envelope — diagnostics, plans,
saves, ops and ``perInput`` — equal for the same script and input, over the ``.stc`` corpus
(the ``tour-*`` cases among it), the sequences ``cli/tests/script/plan_replay_test.zig``
replays, URLs served on loopback, and the refusals. Skips when the CLI is not built.
"""

from __future__ import annotations

import json
import os
import re
import shutil
import subprocess
import tempfile
import unittest
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from threading import Thread

from tests import _PKG_ROOT
from tests.fixtures.test_fixture_script import CASES_PATH, read_cases
from tests.image.test_jpeg_cli import _CLI
from tests.script.test_script_cli import _run

_SAMPLE = _PKG_ROOT.parent / "common" / "samples" / "sample.png"  # 16x12, album
_HEAD = "@source sample.png:\n"
_DOC = "doc.json"
_GREEN = "@use line #00ff00 2px; @line (2,3) (14,9)"

# The replay test's sequences, then the crop units and the point colour it pins.
_SEQUENCES = (
  "@filter aqua; @rect (1,1) (9,9); @undo",
  "@rect (1,1) (9,9); @filter aqua; @undo",
  _GREEN + "; @line (0,0) (15,11); @filter bw; @undo 2",
  _GREEN + "; @crop 25%; @line (1,1) (6,4); @undo 2",
  _GREEN + "; @crop 25%",
  "@crop 25%; @use line #00ff00 2px; @line (1,1) (7,5)",
  _GREEN + "; @crop 2px 1px; @line (0,0) (6,5); @crop 25%; @rect (1,1) (4,3)",
  _GREEN + "; @crop x1=0 x2=50% y1=0 y2=100%; @line (1,1) (6,10)",
  _GREEN + "; @layout " + _DOC + " replace",
  _GREEN + "; @layout " + _DOC + " replace; @undo",
  _GREEN + "; @layout " + _DOC,
  _GREEN + "; @crop x1=1mm x2=-2mm y1=0.5mm y2=-0.05in",
  _GREEN + "; @crop x1=25% x2=-25%",
  _GREEN + "; @crop x1=25% x2=-25% album",
  _GREEN + "; @crop y1=2px y2=-0.1cm; @crop x2=-3px",
  "@use line #00ff00 2px point #ff0000 5; @line (2,3) (14,9)",
  "@use line #00ff00 2px point red 5; @rect (2,3) (14,9); @frame 2; @line (1,1) (2,2)",
  "@filter bw; " * 18 + "@undo 3; @save out.png",
  _GREEN + "; @layout " + _DOC + " replace; @line (1,2) (5,6)",
  _GREEN + "; @layout " + _DOC + "; @line (1,1) (6,4); @undo",
  "@layout " + _DOC + "; @crop 25%; @line (1,1) (6,4)",
  _GREEN + "; @filter bw; @line (0,0) (15,11)",
  _GREEN + "; @crop 25%; @undo",
  _GREEN + "; @crop x1=0 x2=50%; @line (1,1) (6,10); @filter sepia; @rect (2,2) (5,5)",
  _GREEN + "; @line (0,0) (15,11); @filter bw; @line (3,3) (9,9); @undo 3",
  "@filter bw; @layout http://127.0.0.1:1/l.json",  # the refusals, as diagnostics
  "@layout missing.json",
  "@line (0,0) (1,1); " * 201,
)

# Whole scripts over URLs served on loopback ({base}), and over one that never answers.
_URL_SCRIPTS = (
  "@source {base}/sample.png:\n  @line (2,3) (14,9)\n  @crop 25%\n  @line (1,1) (6,4)\n  @save\n",
  "@source {base}/sample.png:\n  @line (2,3) (14,9)\n  @layout {base}/doc.json\n  @crop 10%\n",
  "@source {base}/sample.png:\n  @line (2,3) (14,9)\n  @layout {base}/doc.json replace\n",
  "@source {base}/sample.png:\n  @filter bw\n  @crop x1=1in\n  @save\n",
  "@source http://127.0.0.1:1/a.png:\n  @crop 10%\n  @line (0,0) (1,1)\n",
  "@source {base}/sample.png:\n  @layout {base}/missing.json\n",
)


class _Quiet(SimpleHTTPRequestHandler):
  def log_message(self, *args):
    pass


class CliPlanParityTests(unittest.TestCase):
  @classmethod
  def setUpClass(cls):
    if not _CLI.is_file():
      raise unittest.SkipTest("the CLI is not built at %s" % _CLI)

  def setUp(self):
    tmp = tempfile.TemporaryDirectory()
    self.addCleanup(tmp.cleanup)
    self.dir = tmp.name
    shutil.copyfile(_SAMPLE, os.path.join(self.dir, "sample.png"))
    with open(os.path.join(self.dir, _DOC), "w", encoding="utf-8") as fh:
      fh.write('{"lines":[{"points":[{"x":0,"y":0},{"x":15,"y":11}],"color":"#0000ff"}]}')
    server = ThreadingHTTPServer(("127.0.0.1", 0), partial(_Quiet, directory=self.dir))
    Thread(target=server.serve_forever, daemon=True).start()
    self.addCleanup(server.server_close)
    self.addCleanup(server.shutdown)
    self.base = "http://127.0.0.1:%d" % server.server_address[1]

  def _expect_same(self, script: str, extra: tuple = ()) -> None:
    with open(os.path.join(self.dir, "p.stc"), "w", encoding="utf-8") as fh:
      fh.write(script)
    argv = ["--script-plan", "p.stc", *extra]
    zig = subprocess.run([str(_CLI), *argv], cwd=self.dir, stdout=subprocess.PIPE,
                         stderr=subprocess.PIPE, text=True, timeout=120)
    old = os.getcwd()
    os.chdir(self.dir)
    try:
      code, out, _ = _run(argv)
    finally:
      os.chdir(old)
    self.assertEqual(code, zig.returncode)
    self.assertEqual(json.loads(out), json.loads(zig.stdout))

  def test_the_corpus_plans_as_the_cli_plans_it(self):
    for case in read_cases(CASES_PATH):
      with self.subTest(case=case["name"]):
        # Its URLs point at a port that refuses at once: no suite reaches the network.
        script = re.sub(r"https?://[^/\s\"]+", "http://127.0.0.1:1", case["script"])
        self._expect_same(script, ("-i", "sample.png"))

  def test_url_sources_and_layouts_plan_as_the_cli_plans_them(self):
    for script in _URL_SCRIPTS:
      with self.subTest(script=script[:60]):
        self._expect_same(script.replace("{base}", self.base))

  def test_the_replayed_sequences_plan_as_the_cli_plans_them(self):
    for ops in _SEQUENCES:
      with self.subTest(ops=ops[:60]):
        self._expect_same(_HEAD + ops + "\n")
        self._expect_same(ops.replace("; ", "\n") + "\n", ("-i", "sample.png"))


if __name__ == "__main__":
  unittest.main()
