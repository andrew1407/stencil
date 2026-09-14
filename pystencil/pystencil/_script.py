from __future__ import annotations

"""The ``.stc`` script handle: a ctypes wrapper over the ``stencil_cli_script*`` ABI.

The core parses, lowers and diagnoses; this reads the result out of the handle into the
plain values of :mod:`pystencil._scripttypes`. Every string the ABI returns points into
the handle's own memory, so what a runner needs — diagnostics, blocks, ops — is read
eagerly at parse time, while the colouring tokens, the dump and :meth:`Script.resolve`
read through the handle and refuse once it is closed.
"""

import ctypes
import functools

from ._scripttypes import (
  OP_KINDS, SOURCE_KINDS, TOKEN_KINDS, Block, Blocks, Diagnostic, Diagnostics, Op, Ops,
  Pixels, Token, Tokens,
)
from ._types import NoneType
from .core import Core, get_core

# CSS pixels per cm at 96 dpi — the basis the crop parser and the browser share.
PX_PER_CM = 96.0 / 2.54
# MAX_POINTS_PER_LINE points plus thickness and pointSize (scriptTypes.hpp).
_RESOLVE_CAP = 2 * 200 + 2


class ScriptError(ValueError):
  """A script could not be read, parsed without errors, or applied."""


def _text(raw) -> str:
  return raw.decode("utf-8", "replace") if raw else ""


class Script:
  """One parsed ``.stc`` program. Use it as a context manager, or call :meth:`close`."""

  def __init__(self, handle: int, core: Core) -> None:
    self.__handle = handle
    self.__core = core
    # One buffer for every resolve: one call per crop/line/rect op adds up.
    self.__resolve_buf = (ctypes.c_double * _RESOLVE_CAP)()
    lib = core._lib
    self.diagnostics: Diagnostics = _read_diagnostics(lib, handle)
    self.blocks: Blocks = _read_blocks(lib, handle)
    self.ops: Ops = _read_ops(lib, handle)

  @classmethod
  def parse(cls, text: str, core: (Core | NoneType) = None) -> "Script":
    """Parse ``text``; the returned program carries its own diagnostics."""
    core = core or get_core()
    raw = text.encode("utf-8")
    handle = core._lib.stencil_cli_scriptParse(raw, len(raw))
    if not handle: raise ScriptError("the core refused to parse that script")
    return cls(handle, core)

  @functools.cached_property
  def tokens(self) -> Tokens:
    """The editor colouring classes, read on first use — only a highlighter wants them."""
    return _read_tokens(self.__lib(), self.__handle)

  @functools.cached_property
  def dump(self) -> str:
    """The canonical dump the fixture corpus records, serialised on first use."""
    return _text(self.__lib().stencil_cli_scriptDump(self.__handle))

  def close(self) -> None:
    if self.__handle:
      self.__core._lib.stencil_cli_scriptDestroy(self.__handle)
      self.__handle = 0

  def __enter__(self) -> "Script":
    return self

  def __exit__(self, *exc) -> bool:
    self.close()
    return False

  def __del__(self) -> None:
    try:
      self.close()
    except Exception:  # interpreter teardown may already have dropped the lib
      pass

  @property
  def has_errors(self) -> bool:
    return any(d.severity == "error" for d in self.diagnostics)

  def diagnostics_text(self) -> str:
    """The fixture corpus' diagnostics section (empty when there are none)."""
    return "".join(d.dump_line() + "\n" for d in self.diagnostics)

  def block_ops(self, block: Block) -> Ops:
    """The ops belonging to ``block``, in order."""
    return self.ops[block.op_start:block.op_start + block.op_count]

  def resolve(self, index: int, image_w: float, image_h: float) -> Pixels:
    """One op's length tokens in pixels against the CURRENT image size.

    A crop earlier in the block already moved the frame, so pass what you hold now.
    """
    buf = self.__resolve_buf
    n = self.__lib().stencil_cli_scriptOpResolve(
      self.__handle, index, float(image_w), float(image_h), PX_PER_CM, PX_PER_CM,
      buf, _RESOLVE_CAP,
    )
    if n < 0: raise ScriptError("op %d resolves to nothing" % index)
    return [buf[i] for i in range(n)]

  def __lib(self):
    """The bound library, refused once the handle has been destroyed."""
    if not self.__handle: raise ScriptError("this script handle is closed")
    return self.__core._lib


def parse_script(text: str, core: (Core | NoneType) = None) -> Script:
  """Parse ``.stc`` source into a :class:`Script`."""
  return Script.parse(text, core)


def _read_diagnostics(lib, handle: int) -> Diagnostics:
  out = list()
  sev, line, col, length = (ctypes.c_int() for _ in range(4))
  code = ctypes.c_char_p()
  for i in range(max(0, lib.stencil_cli_scriptDiagCount(handle))):
    msg = lib.stencil_cli_scriptDiagAt(
      handle, i, ctypes.byref(sev), ctypes.byref(line), ctypes.byref(col),
      ctypes.byref(length), ctypes.byref(code),
    )
    out.append(Diagnostic(
      "warning" if sev.value else "error", _text(code.value),
      line.value, col.value, length.value, _text(msg),
    ))
  return tuple(out)


def _read_tokens(lib, handle: int) -> Tokens:
  out = list()
  kind, line, col, length = (ctypes.c_int() for _ in range(4))
  for i in range(max(0, lib.stencil_cli_scriptTokenCount(handle))):
    if not lib.stencil_cli_scriptTokenAt(
      handle, i, ctypes.byref(kind), ctypes.byref(line), ctypes.byref(col),
      ctypes.byref(length),
    ):
      continue
    out.append(Token(TOKEN_KINDS[kind.value], line.value, col.value, length.value))
  return tuple(out)


def _read_blocks(lib, handle: int) -> Blocks:
  out = list()
  kind, frame, start, count = (ctypes.c_int() for _ in range(4))
  for i in range(max(0, lib.stencil_cli_scriptBlockCount(handle))):
    src = lib.stencil_cli_scriptBlockAt(
      handle, i, ctypes.byref(kind), ctypes.byref(frame), ctypes.byref(start),
      ctypes.byref(count),
    )
    out.append(Block(i, _text(src), SOURCE_KINDS[kind.value], frame.value,
                     start.value, count.value))
  return tuple(out)


def _read_ops(lib, handle: int) -> Ops:
  out = list()
  kind, block, edit, line, col, nstr, nnum = (ctypes.c_int() for _ in range(7))
  num = ctypes.c_double()
  for i in range(max(0, lib.stencil_cli_scriptOpCount(handle))):
    if not lib.stencil_cli_scriptOpAt(
      handle, i, ctypes.byref(kind), ctypes.byref(block), ctypes.byref(edit),
      ctypes.byref(line), ctypes.byref(col), ctypes.byref(nstr), ctypes.byref(nnum),
    ):
      continue
    strs = tuple(_text(lib.stencil_cli_scriptOpStr(handle, i, k))
                 for k in range(nstr.value))
    toks = tuple(_text(lib.stencil_cli_scriptOpTok(handle, i, k))
                 for k in range(max(0, lib.stencil_cli_scriptOpTokCount(handle, i))))
    nums = list()
    for k in range(nnum.value):
      if lib.stencil_cli_scriptOpNum(handle, i, k, ctypes.byref(num)):
        nums.append(num.value)
    out.append(Op(i, OP_KINDS[kind.value], block.value, edit.value, line.value,
                  col.value, strs, toks, tuple(nums)))
  return tuple(out)
