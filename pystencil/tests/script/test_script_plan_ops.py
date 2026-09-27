"""``--script-plan``'s per-block ``ops``: the lowered stream with every length as written, in
the CLI's shape (``cli/src/script/plan/block.zig``); ``perInput``, each input planned against
its own size; a template-body diagnostic's ``related`` call; and no ``redo`` anywhere
downstream of the lowerer, which resolves every ``@redo`` statically (stc-contract §7)."""

from __future__ import annotations

import json
import os

from pystencil import scriptpaths
from pystencil._script import parse_script
from pystencil.editor import Editor
from pystencil.cli.plansequence import ACTIONS
from pystencil.editor.script import HANDLERS
from tests.fixtures.test_fixture_script import CASES_PATH, read_cases
from tests.helpers.nativecase import NativeCase
from tests.script.test_script_cli import _run, _ScriptFixture

_SCRIPT = (
  "@source shots/b.png:\n  @crop x1=10% aspect=3:2 album\n  @filter sepia\n"
  "  @use line #ff000080, 2.5, dashed\n  @rect (10%, 5px) (-10%, -1in)\n"
  "  @undo\n  @redo\n  @undo 1\n  @save out/\n"
)

# The CLI's own output for _SCRIPT, byte for byte: the undo lowers to one op of 3 steps
# followed by the replay of the edits that stay live.
_CLI_OPS = """[
{"kind":"open","line":1,"edit":0,"strs":["shots/b.png"],"toks":[],"nums":[1]},
{"kind":"crop","line":2,"edit":1,"strs":["3:2"],"toks":["10%","","",""],"nums":[1]},
{"kind":"filter","line":3,"edit":2,"strs":["sepia",""],"toks":[],"nums":[]},
{"kind":"rect","line":5,"edit":3,"strs":["#ff000080","dashed","transparent",""],\
"toks":["10%","5px","-10%","-1in"],"nums":[2.5,4,1]},
{"kind":"undo","line":9,"edit":0,"strs":[],"toks":[],"nums":[3]},
{"kind":"filter","line":3,"edit":2,"strs":["sepia",""],"toks":[],"nums":[]},
{"kind":"rect","line":5,"edit":3,"strs":["#ff000080","dashed","transparent",""],\
"toks":["10%","5px","-10%","-1in"],"nums":[2.5,4,1]},
{"kind":"save","line":9,"edit":0,"strs":["out/"],"toks":[],"nums":[]}
]""".replace("\n", "")


class PlanOpsTests(_ScriptFixture):
  """Runs inside the ``shots/`` fixture, so a local block has real dims to resolve."""

  def _out(self, body):
    self._write("p.stc", body)
    code, out, _ = _run(["--script-plan", "p.stc"])
    self.assertEqual(code, 0)
    return out.rstrip()

  def _plan(self, body):
    return json.loads(self._out(body))

  def _ops_text(self, body):
    """A one-block envelope's raw ``ops`` value, which ``perInput`` follows as the last key."""
    out = self._out(body)
    self.assertTrue(out.endswith("]}]}"))
    return out[out.index('"ops":') + len('"ops":'):out.index(',"perInput":')]

  def test_the_ops_match_the_cli_byte_for_byte(self):
    self.assertEqual(self._ops_text(_SCRIPT), _CLI_OPS)

  def test_every_block_carries_its_own_ops_after_its_saves(self):
    plan = self._plan(
      "@source shots/a.png:\n    @filter bw\n    @save\n@source shots/b.png:\n    @save\n")
    self.assertEqual([list(b)[-3:] for b in plan["blocks"]], [["saves", "ops", "perInput"]] * 2)
    self.assertEqual([[op["kind"] for op in b["ops"]] for b in plan["blocks"]],
                     [["open", "filter", "save"], ["open", "save"]])

  def test_lengths_stay_as_written_while_the_plan_resolves_them(self):
    plan = self._plan("@source shots/a.png:\n    @line (0,0) (50%,1cm)\n    @save\n")
    block = plan["blocks"][0]
    line = next(op for op in block["ops"] if op["kind"] == "line")
    self.assertEqual(line["toks"], ["0px", "0px", "50%", "1cm"])
    layout = next(a for a in block["plans"][0]["actions"] if a["op"] == "layout")
    self.assertEqual(layout["lines"][0]["points"][1]["x"], 20)

  def test_a_url_source_that_draws_and_does_not_fetch_is_refused(self):
    self._write("p.stc", "@source http://127.0.0.1:1/b.png:\n  @filter bw\n  @rect (1,1) (4,4)\n")
    code, out, _ = _run(["--script-plan", "p.stc"])
    plan = json.loads(out)
    self.assertEqual((code, plan["blocks"]), (1, list()))
    diag = plan["diagnostics"][0]
    self.assertEqual((diag["code"], diag["line"], diag["len"]), ("E_PLAN_SOURCE_UNREADABLE", 3, 5))

  def test_a_url_sources_bare_save_lands_in_the_working_directory(self):
    plan = self._plan("@source https://e.example/pics/b.png?v=2:\n    @save\n")
    self.assertEqual(plan["blocks"][0]["saves"][0]["path"], "b-stencil.png")
    self.assertEqual(scriptpaths.resolve_target("", "http://h:8/x/c.png", 4, "png"),
                     "c-frame-4-stencil.png")


class CropTokenTests(_ScriptFixture):
  """A plan's crop reads as ``--script`` resolves it, never by an executor's own page."""

  def _spec(self, body):
    self._write("p.stc", body)
    _, out, _ = _run(["--script-plan", "p.stc"])
    actions = json.loads(out)["blocks"][0]["plans"][0]["actions"]
    return next(a for a in actions if a["op"] == "crop")["spec"]

  def test_an_absolute_unit_goes_out_as_its_px_the_far_edge_sign_kept(self):
    spec = self._spec("@source shots/a.png:\n  @crop x1=1cm x2=-5mm y1=10% y2=-2px\n")
    self.assertEqual(spec, {"x1": "37.79527559055118px", "x2": "-18.89763779527559px",
                            "y1": "10%", "y2": "-2px"})

  def test_a_one_axis_crop_gains_the_axis_the_script_derives(self):
    spec = self._spec("@source shots/a.png:\n  @crop x1=10% x2=-10% album\n")
    self.assertEqual(spec, {"x1": "10%", "x2": "-10%", "y1": "0px", "y2": "24px"})
    spec = self._spec("@source https://e.example/a.png:\n  @crop y2=50% album\n")
    self.assertEqual(spec, {"y2": "50%", "album": True})  # no size to derive from


class PerInputTests(_ScriptFixture):
  """A directory of two sizes: the block-level fields keep the first input, ``perInput`` each."""

  def test_each_input_is_planned_against_its_own_size(self):
    Editor().blank(80, 60).save(os.path.join("shots", "b.png"))
    self._write("p.stc", "@source shots/:\n    @line (0,0) (50%,50%)\n    @save out/\n")
    code, out, _ = _run(["--script-plan", "p.stc"])
    self.assertEqual(code, 0)
    block = json.loads(out)["blocks"][0]
    self.assertEqual(block["dims"], {"width": 40, "height": 30})
    per = block["perInput"]
    self.assertEqual([e["input"] for e in per], block["inputs"])
    self.assertEqual([e["dims"]["width"] for e in per], [40, 80])
    self.assertEqual(per[0]["plans"], block["plans"])
    for entry, half in zip(per, (20, 40)):
      actions = entry["plans"][0]["actions"]
      self.assertEqual(actions[0], {"op": "openFile", "path": entry["input"]})
      layout = next(a for a in actions if a["op"] == "layout")
      self.assertEqual(layout["lines"][0]["points"][1]["x"], half)
      self.assertEqual([s["input"] for s in entry["saves"]], [entry["input"]])

  def test_a_template_body_diagnostic_names_its_call_as_related(self):
    self._write("p.stc", "@stencil bad:\n  @line (0,0)\n\n@source shots/a.png:\n"
                "  @use stencil bad\n")
    code, out, _ = _run(["--script-plan", "p.stc"])
    self.assertEqual(code, 1)
    diag = json.loads(out)["diagnostics"][0]
    self.assertEqual(list(diag)[-1], "related")
    self.assertEqual(diag["related"], {"line": 5, "col": 3, "len": 4})
    self.assertTrue(diag["message"].endswith("(from the @use stencil at 5:3)"))


class NoRedoTests(NativeCase):
  def test_the_lowerer_never_emits_a_redo_across_the_corpus(self):
    cases = read_cases(CASES_PATH)
    self.assertTrue(any("@redo" in case["script"] for case in cases))
    for case in cases:
      with self.subTest(case=case["name"]), parse_script(case["script"]) as program:
        self.assertNotIn("redo", [op.kind for op in program.ops])

  def test_no_runner_table_carries_a_redo(self):
    self.assertNotIn("redo", HANDLERS)
    self.assertNotIn("redo", ACTIONS)
    self.assertEqual(set(HANDLERS), set(ACTIONS))


if __name__ == "__main__":
  import unittest

  unittest.main()
