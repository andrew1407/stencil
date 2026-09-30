"""Cross-surface fixture-conformance walkers (pystencil side).

Walks the shared, language-neutral fixture corpus under common/fixtures/
(see each family's _schema.md) through pystencil's REAL entry points and pins
the current behavior. Measured pystencil-vs-corpus divergences live in
tests/helpers/fixture_overrides.json — the shared fixtures are never edited here.
"""

from __future__ import annotations

import base64
import json
import math
from pathlib import Path

from pystencil.layout import Line

# Shared roots, __file__-relative: pystencil/tests/helpers → repo root → common/.
_COMMON = Path(__file__).resolve().parent.parent.parent.parent / "common"
_CONFIG = _COMMON / "config"
_FIXTURES = _COMMON / "fixtures"
_LLM_FIXTURES = _FIXTURES / "llm"

with open(Path(__file__).resolve().parent / "fixture_overrides.json", encoding="utf-8") as _fh:
  _OVERRIDES = json.load(_fh)


def _load(path: Path):
  with open(path, encoding="utf-8") as fh:
    return json.load(fh)


def _opplan_text(fx: dict) -> str:
  """An op-plan case's reply as a model would send it: verbatim, object-serialised, chunks
  repeated (``parts``) or raw bytes decoded as a reply arrives (``inputBase64``)."""
  if "parts" in fx: return "".join(chunk * n for chunk, n in fx["parts"])
  if "inputBase64" in fx: return base64.b64decode(fx["inputBase64"]).decode("utf-8", "replace")
  return fx["input"] if isinstance(fx["input"], str) else json.dumps(fx["input"])


def _norm(v):
  """JSON-structural normalization: NaN floats become the string 'NaN'."""
  if isinstance(v, float) and math.isnan(v):
    return "NaN"
  if isinstance(v, dict):
    return {k: _norm(x) for k, x in v.items()}
  if isinstance(v, list):
    return [_norm(x) for x in v]
  return v


def _filled_line_dict(line: Line) -> dict:
  """A parsed Line as the corpus's 'filled' shape (pointColor always present)."""
  d = line.to_dict()
  d.setdefault("pointColor", line.point_color)
  return d
