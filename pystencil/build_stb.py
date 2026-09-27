"""The stb headers the codec units compile against, never committed: obtained at build time
from the upstream commit cli/build.zig.zon pins, checked against the SHA-256 in stb/pin.json
and kept in the gitignored stb/cache/. A header that fails its hash is refused and deleted.

Loaded by path from build.py, like build_compile.py.
"""

from __future__ import annotations

import hashlib
import json
import os
import re
import sys
import tarfile
from pathlib import Path

RAW_URL = "https://raw.githubusercontent.com/nothings/stb/%s/%s"
FETCH_TIMEOUT = 30.0

_ZON_STB = re.compile(
  r'\.stb\s*=\s*\.\{\s*\.url\s*=\s*"git\+https://github\.com/nothings/stb\.git#([0-9a-f]{40})"'
  r',\s*\.hash\s*=\s*"([^"]+)"')


def read_pin(pin_path: Path) -> dict:
  """``{"commit": …, "sha256": {name: hex}}`` as recorded in the repo."""
  return json.loads(pin_path.read_text(encoding="utf-8"))


def zon_stb(zon_path: Path):
  """``(commit, package hash)`` of the CLI's stb dependency, or None when it is unreadable."""
  try:
    found = _ZON_STB.search(zon_path.read_text(encoding="utf-8"))
  except OSError:
    return None
  return (found.group(1), found.group(2)) if found else None


def zig_cache_dir() -> Path:
  """Zig's global cache, resolved as zig itself does outside Windows."""
  if os.environ.get("ZIG_GLOBAL_CACHE_DIR"): return Path(os.environ["ZIG_GLOBAL_CACHE_DIR"])
  if os.environ.get("XDG_CACHE_HOME"): return Path(os.environ["XDG_CACHE_HOME"]) / "zig"
  return Path.home() / ".cache" / "zig"


def zig_copy(package_hash: str, name: str):
  """The CLI's fetched copy of ``name`` — an unpacked package dir or its .tar.gz — or None."""
  root = zig_cache_dir() / "p"
  try:
    unpacked = root / package_hash / name
    if unpacked.is_file(): return unpacked.read_bytes()
    packed = root / (package_hash + ".tar.gz")
    if not packed.is_file(): return None
    with tarfile.open(str(packed), "r:gz") as tar:
      member = tar.extractfile("%s/%s" % (package_hash, name))
      return member.read() if member is not None else None
  except (OSError, KeyError, tarfile.TarError):
    return None


def download(commit: str, name: str) -> bytes:
  """``name`` at ``commit`` from the upstream repo, through the package's one fetch guard."""
  from pystencil import _net
  return _net._fetch(RAW_URL % (commit, name), strict=True, timeout=FETCH_TIMEOUT)


def sha256(data: bytes) -> str:
  return hashlib.sha256(data).hexdigest()


def _write(dest: Path, data: bytes) -> None:
  """Replace ``dest`` in one rename, so a reader never sees half a header."""
  tmp = dest.with_name("%s.%d.tmp" % (dest.name, os.getpid()))
  tmp.write_bytes(data)
  os.replace(tmp, dest)


def _obtain(dest: Path, digest: str, sources: list, note) -> bool:
  """Keep a verified ``dest``, else take the first source whose bytes match ``digest``."""
  if dest.is_file():
    if sha256(dest.read_bytes()) == digest: return True
    dest.unlink()
    note("deleted %s: it does not match its pinned sha256" % dest.name)
  for label, fetch in sources:
    try:
      data = fetch()
    except Exception as exc:  # offline, refused by the guard, or no package to import
      note("%s unavailable from %s: %s" % (dest.name, label, exc))
      continue
    if data is None: continue
    if sha256(data) != digest:
      note("refused %s from %s: sha256 %s is not the pinned one" % (dest.name, label, sha256(data)))
      continue
    _write(dest, data)
    return True
  return False


def ensure(cache_dir: Path, pin_path: Path, zon_path: Path, verbose: bool = False):
  """The directory holding every pinned header, verified, or None when one cannot be had.

  Order per file: the cache, then the CLI's zig package copy (only while its commit is the
  pinned one), then the upstream download.
  """
  note = (lambda text: print("note: stb: " + text, file=sys.stderr)) if verbose else (lambda _t: None)
  try:
    pin = read_pin(pin_path)
    commit, digests = pin["commit"], pin["sha256"]
    cache_dir.mkdir(parents=True, exist_ok=True)
  except (OSError, ValueError, KeyError, TypeError) as exc:
    note("no usable pin or cache (%s); building without the stb codec" % exc)
    return None
  zon = zon_stb(zon_path)
  package = zon[1] if zon is not None and zon[0] == commit else None
  for name, digest in sorted(digests.items()):
    sources = list()
    if package is not None:
      sources.append(("the zig cache", lambda n=name: zig_copy(package, n)))
    sources.append(("github.com/nothings/stb", lambda n=name: download(commit, n)))
    if not _obtain(cache_dir / name, digest, sources, note):
      note("%s could not be obtained; building without the stb codec" % name)
      return None
  return cache_dir
