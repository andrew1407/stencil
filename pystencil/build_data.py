"""Copy the shared tables pystencil reads at import from common/config into pystencil/_data/.

They are never checked in: importing the package and build.py refresh them, and
tests/test_canonical_drift.py pins each copy against its common/config original.
"""

from __future__ import annotations

from pathlib import Path

FILES = ("llm/providers.json", "llm/systemPrompt.json", "llm/opRegistry.json",
         "net/blockedRanges.json")


def sync(common_config: Path, data_dir: Path) -> Path:
  """Write each table into `data_dir` when its bytes differ; return `data_dir`.

  An installed package carries its copies and no common/ beside it, so nothing is touched.
  """
  if not common_config.is_dir():
    return data_dir
  data_dir.mkdir(parents=True, exist_ok=True)
  for rel in FILES:
    src = (common_config / rel).read_bytes()
    dst = data_dir / Path(rel).name
    if not dst.exists() or dst.read_bytes() != src:
      dst.write_bytes(src)
  return data_dir
