"""The compile step behind build.py: the $CXX driver, the file lock that serializes racing
builds, and the compiler runs that write the shared library — each C unit to an object, then
one link of the core with those objects.

Loaded by path from build.py (never via sys.path), and one of its build inputs: it carries
the compiler flags.
"""

from __future__ import annotations

import contextlib
import os
import subprocess
import sys
import tempfile
import time
from pathlib import Path


def compiler() -> str:
  """The C++ driver to invoke. Honour $CXX so callers can pin a toolchain; default c++."""
  return os.environ.get("CXX", "c++")


def child_env() -> dict:
  """The environment a compiler runs under: without ``STENCIL_LLM_*`` and the server tokens,
  which nothing spawned has any business inheriting (twin of ``cli/src/safety/child.zig``)."""
  tokens = ("STENCIL_SERVER_TOKEN", "STENCIL_SERVER_TOKENS")
  return {k: v for k, v in os.environ.items()
          if not k.upper().startswith("STENCIL_LLM_") and k.upper() not in tokens}


@contextlib.contextmanager
def build_lock(path: Path):
  """Hold an exclusive lock on ``path`` so concurrent processes build one at a time."""
  with open(path, "a+b") as handle:
    try:
      import fcntl
      fcntl.flock(handle.fileno(), fcntl.LOCK_EX)
    except ImportError:  # Windows: msvcrt retries for ~10 s, then raises OSError
      import msvcrt
      handle.seek(0)
      msvcrt.locking(handle.fileno(), msvcrt.LK_LOCK, 1)
    yield  # closing the handle releases either lock


def _run(cmd: list, cwd: Path, verbose: bool, deadline: float, timeout: float) -> None:
  """One compiler run, bounded by what is left of the whole build's ``timeout``."""
  if verbose:
    print("building:", " ".join(cmd), file=sys.stderr)
  try:
    proc = subprocess.run(
      cmd,
      cwd=str(cwd),
      env=child_env(),
      stdout=subprocess.PIPE,
      stderr=subprocess.PIPE,
      text=True,
      timeout=max(deadline - time.monotonic(), 0.001),
    )
  except subprocess.TimeoutExpired:
    raise RuntimeError(
      "compiling the stencil core timed out after %d s (compiler=%s)"
      % (timeout, compiler())
    ) from None
  if proc.returncode != 0:
    raise RuntimeError(
      "failed to compile stencil core shared library "
      "(compiler=%s):\n%s" % (compiler(), proc.stderr)
    )
  if verbose and proc.stderr:
    print(proc.stderr, file=sys.stderr)


def compile_core(dest: Path, verbose: bool, core_dir: Path, sources: list, include_dirs: list,
                 timeout: float, c_sources: tuple = (), c_include_dirs: tuple = ()) -> None:
  """Write the shared library to ``dest``: every C unit compiled on its own, then one link.

  The C++ ``-std`` would refuse a C unit in the same run, so each goes through ``-x c`` to
  a temp object first; relative core paths resolve from ``core_dir``.
  """
  deadline = time.monotonic() + timeout
  with tempfile.TemporaryDirectory(prefix="pystencil-build-") as objdir:
    objects = list()
    for src in c_sources:
      obj = str(Path(objdir) / (Path(src).stem + ".o"))
      cmd = [compiler(), "-x", "c", "-std=c11", "-O2", "-fPIC"]
      cmd.extend("-I" + str(inc) for inc in c_include_dirs)
      cmd.extend(["-c", str(src), "-o", obj])
      _run(cmd, core_dir, verbose, deadline, timeout)
      objects.append(obj)

    cmd = [compiler(), "-std=c++17", "-O2", "-fPIC", "-shared"]
    cmd.extend("-I" + inc for inc in include_dirs)
    cmd.extend(sources)
    cmd.extend(objects)
    cmd.extend(["-o", str(dest)])
    _run(cmd, core_dir, verbose, deadline, timeout)
