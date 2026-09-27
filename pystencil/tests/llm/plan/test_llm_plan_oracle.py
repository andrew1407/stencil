"""The typed op-plan oracle: parse_op_plan's whole result over the shared corpus, pinned.

Each hand-written, generated and adversarial (``oracle/inputs.json``) case records its verdict,
reply, warnings, error and typed actions / variants / ask in ``tests/goldens/opplan_oracle.json``;
``ORACLE_WRITE=1`` re-records it.
"""

from __future__ import annotations

import dataclasses
import hashlib
import json
import os
import unittest
from pathlib import Path

from tests.helpers.fixturebase import _LLM_FIXTURES, _load, _opplan_text
from tests.helpers.nativecase import NativeCase

from pystencil.llm import LlmError, parse_op_plan

_OPPLAN = _LLM_FIXTURES / "opPlan"
_GOLDEN = Path(__file__).resolve().parents[2] / "goldens" / "opplan_oracle.json"
# A string past this many code units is pinned by its length and digest.
_LONG = 160


def _cases() -> list:
  hand = _load(_OPPLAN / "cases.json")["cases"]
  generated = _load(_OPPLAN / "generated" / "cases.json")["cases"]
  oracle = _load(_OPPLAN / "oracle" / "inputs.json")["cases"]
  return [(fx.get("file") or fx["name"], _opplan_text(fx)) for fx in hand + generated + oracle]


def _pin(v):
  if isinstance(v, str) and len(v) > _LONG:
    digest = hashlib.sha256(v.encode("utf-8", "surrogatepass")).hexdigest()[:16]
    return "<%d chars sha256:%s>" % (len(v), digest)
  if isinstance(v, dict): return {k: _pin(x) for k, x in v.items()}
  if isinstance(v, list): return [_pin(x) for x in v]
  return v


def typed_result(text: str) -> dict:
  """Everything a caller can read off one parse, as plain JSON values."""
  try:
    plan = parse_op_plan(text)
  except LlmError as e:
    return _pin({"verdict": "invalid", "error": str(e)})
  except Exception as e:  # anything else escaping the parser is recorded, not hidden
    return {"verdict": "raised", "error": type(e).__name__}
  out = {
    "verdict": "valid",
    "reply": plan.reply,
    "warnings": plan.warnings,
    "actions": plan.actions,
    "variants": [{"label": v.label, "actions": v.actions} for v in plan.variants],
  }
  if plan.ask is not None: out["ask"] = dataclasses.asdict(plan.ask)
  return _pin(out)


def _dump(results: dict) -> str:
  rows = (json.dumps(k) + ": " + json.dumps(v, sort_keys=True) for k, v in results.items())
  return "{\n" + ",\n".join(rows) + "\n}\n"


class OpPlanOracleTest(NativeCase):
  def test_typed_results_match_the_recorded_oracle(self):
    results = {name: typed_result(text) for name, text in _cases()}
    if os.environ.get("ORACLE_WRITE") == "1":
      _GOLDEN.write_text(_dump(results), encoding="utf-8")
      self.skipTest("re-recorded %s" % _GOLDEN.name)
    self.assertTrue(_GOLDEN.is_file(), "missing %s — rerun with ORACLE_WRITE=1" % _GOLDEN)
    want = json.loads(_GOLDEN.read_text(encoding="utf-8"))
    self.assertEqual(sorted(results), sorted(want))
    for name, got in results.items():
      with self.subTest(case=name):
        # Compared as text: 2 and 2.0 are equal values but a different result.
        self.assertEqual(json.dumps(got, sort_keys=True), json.dumps(want[name], sort_keys=True))


if __name__ == "__main__":
  unittest.main()
