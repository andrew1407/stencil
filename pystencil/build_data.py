"""Copy the shared tables pystencil reads at import from common/config into pystencil/_data/.

They are never checked in: importing the package and build.py refresh them, and
tests/test_canonical_drift.py pins each copy against its common/config original.
"""

from __future__ import annotations

import contextlib
import importlib.util
import os
import tempfile
from pathlib import Path

FILES = ("llm/providers.json", "llm/systemPrompt.json", "llm/opRegistry.json",
         "net/blockedRanges.json")
LOCK_NAME = ".sync.lock"


def _build_lock(path: Path):
  spec = importlib.util.spec_from_file_location(
    "pystencil_build_compile", Path(__file__).resolve().parent / "build_compile.py")
  module = importlib.util.module_from_spec(spec)
  spec.loader.exec_module(module)
  return module.build_lock(path)


def _stale(common_config: Path, data_dir: Path) -> list:
  out = list()
  for rel in FILES:
    src = (common_config / rel).read_bytes()
    dst = data_dir / Path(rel).name
    if not dst.exists() or dst.read_bytes() != src:
      out.append((dst, src))
  return out


def _replace(dst: Path, data: bytes) -> None:
  fd, tmp = tempfile.mkstemp(prefix=dst.name + ".", suffix=".tmp", dir=dst.parent)
  try:
    with os.fdopen(fd, "wb") as handle:
      handle.write(data)
    os.replace(tmp, dst)
  finally:
    with contextlib.suppress(OSError):
      os.unlink(tmp)


def sync(common_config: Path, data_dir: Path) -> Path:
  """Write each table into `data_dir` when its bytes differ; return `data_dir`.

  An installed package carries its copies and no common/ beside it, so nothing is touched.
  Writers serialize on `LOCK_NAME` and each copy lands by one rename, so a reader in any
  process sees the old bytes or the new ones, never a torn file.
  """
  if not common_config.is_dir() or not _stale(common_config, data_dir):
    return data_dir
  data_dir.mkdir(parents=True, exist_ok=True)
  with _build_lock(data_dir / LOCK_NAME):
    for dst, src in _stale(common_config, data_dir):
      _replace(dst, src)
  return data_dir
