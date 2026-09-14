from __future__ import annotations

"""Which file a ``.stc`` is read from, what a ``@source`` names, where a ``@save`` writes.

The core classifies a source spec and hands back the save target verbatim; turning
either into concrete paths is the adapter's job, so directory listing, the one-segment
glob, the ``-stencil`` naming rule and the two ``..`` refusals live here. Every door
that reads a script file goes through :func:`read_script`. Twin of
``cli/src/script/sources.zig`` and ``cli/src/script/save.zig``.
"""

import fnmatch
import os
from pathlib import Path

from . import codecs
from ._script import ScriptError
from ._types import NoneType

MAX_INPUTS = 512
MAX_SCRIPT_BYTES = 4 << 20
SUFFIX = "-stencil"
# The codecs this surface can write, so the only extensions a @save may produce.
SAVE_FORMATS = ("png", "bmp")


def is_media(name: str) -> bool:
  """True for a name this surface can actually open. Narrower than the CLI's set:
  codecs live in the adapters, so a directory picks up only what ``codecs`` decodes."""
  return codecs.format_from_ext(name) is not None


def is_url(spec: str) -> bool:
  """True for the http(s) URLs this package fetches — the only remote scheme."""
  low = spec.lower()
  return low.startswith("http://") or low.startswith("https://")


def has_foreign_scheme(spec: str) -> bool:
  """True for a scheme that is neither http(s) nor a bare path — refused up front."""
  head = spec.split("/", 1)[0]
  if ":" not in head: return False
  scheme = head.split(":", 1)[0].lower()
  return scheme not in ("http", "https") and len(scheme) > 1


def has_traversal(path: str) -> bool:
  """True when any segment is ``..`` — refused whether reading or writing."""
  return ".." in path.replace("\\", "/").split("/")


def read_script(path: str) -> str:
  """Read a ``.stc`` file, refusing a path that walks out through ``..``."""
  if has_traversal(path): raise ScriptError("refusing to read through '..': %s" % path)
  with open(path, "r", encoding="utf-8") as handle:
    text = handle.read(MAX_SCRIPT_BYTES + 1)
  if len(text) > MAX_SCRIPT_BYTES: raise ScriptError("that script is too large: %s" % path)
  return text


def _split_dir(spec: str) -> tuple:
  """``dir/leaf`` for a glob spec; a bare leaf lives in ``./``."""
  head, sep, leaf = spec.rpartition("/")
  return (head + sep, leaf) if sep else ("./", spec)


def expand_source(spec: str, kind: str) -> list:
  """Every input ``spec`` names, sorted so a directory or glob runs in a stable order."""
  if kind in ("file", "url"):
    if kind == "file" and has_foreign_scheme(spec):
      raise ScriptError("unsupported source scheme: %s" % spec)
    return [spec]
  if kind == "project": return list()
  directory, leaf = (spec, "*") if kind == "dir" else _split_dir(spec)
  try:
    # Name-sorted here is path-sorted below: every match shares the one directory prefix.
    entries = sorted(p.name for p in Path(directory or ".").iterdir() if p.is_file())
  except OSError:
    raise ScriptError("no such source: %s" % spec)
  found = list()
  for name in entries:
    if name.startswith(".") or not fnmatch.fnmatchcase(name, leaf): continue
    if not is_media(name): continue
    if len(found) >= MAX_INPUTS:
      raise ScriptError("%s matches more than %d files" % (spec, MAX_INPUTS))
    found.append(os.path.join(directory, name))
  return found


def _strip_query(path: str) -> str:
  for mark in ("?", "#"):
    cut = path.find(mark)
    if cut >= 0: path = path[:cut]
  return path


def base_name(path: str) -> str:
  return os.path.basename(_strip_query(path).replace("\\", "/"))


def dir_of(path: str) -> str:
  head, sep, _ = _strip_query(path).replace("\\", "/").rpartition("/")
  return head + sep if sep else ""


def _stem(name: str) -> str:
  root, ext = os.path.splitext(name)
  return name if not ext or not root else root


def save_format(source_ext: (str | NoneType)) -> str:
  """The codec a ``@save`` falls back to for a result derived from ``source_ext``: the
  source's own when this surface can write it, else PNG. One rule, so the name
  ``--script-plan`` reports and the file ``--script`` writes can never disagree."""
  ext = (source_ext or "").lower()
  return ext if ext in SAVE_FORMATS else "png"


def resolve_target(target: str, source: str, frame: (int | NoneType), ext: str) -> str:
  """The path a ``@save <target>`` writes, for a result derived from ``source``.

  ``""`` lands beside the source as ``<stem>-stencil.<ext>``, a trailing ``/`` moves
  that into the named directory, a bare name is taken as given (no suffix) and gains
  the extension, and a path with a known image extension is used verbatim.
  """
  src_base = base_name(source)
  stem = _stem(src_base) if src_base else "image"
  if frame is not None: stem = "%s-frame-%d" % (stem, frame)
  if not target: return "%s%s%s.%s" % (dir_of(source), stem, SUFFIX, ext)
  if target[-1] in ("/", "\\"): return "%s%s%s.%s" % (target, stem, SUFFIX, ext)
  if codecs.format_from_ext(base_name(target)) is not None: return target
  return "%s.%s" % (target, ext)


def guard_target(path: str, confine_output: bool) -> None:
  """``..`` is always refused; under confinement so is anything outside the cwd."""
  if has_traversal(path): raise ScriptError("refusing to save through '..': %s" % path)
  if not confine_output: return
  cwd = os.path.realpath(os.getcwd())
  full = os.path.realpath(os.path.join(cwd, path))
  if full != cwd and not full.startswith(cwd + os.sep):
    raise ScriptError("refusing to save outside the working directory: %s" % path)
