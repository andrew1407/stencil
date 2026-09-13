from __future__ import annotations

"""The ``.stc`` script handle: a ctypes wrapper over the ``stencil_cli_script*`` ABI.

The core parses, lowers and diagnoses; this reads the result out of the handle into
plain Python values. Every string the ABI returns points into the handle's own memory,
so each accessor is read eagerly at parse time and the handle keeps only the two calls
that need it live — :meth:`Script.resolve` and the memoised dump.
"""

import ctypes
from dataclasses import dataclass, field

from ._types import NoneType
from .core import Core, get_core

# Index order is the ABI's; never reorder, only append (scriptTypes.hpp).
TOKEN_KINDS = tuple(
  "comment directive keyword number unit color string param punct ident error".split())
SOURCE_KINDS = tuple("project file url dir glob".split())
OP_KINDS = tuple("open frame crop filter line rect layout save undo redo".split())

# CSS pixels per cm at 96 dpi — the basis the crop parser and the browser share.
PX_PER_CM = 96.0 / 2.54
# MAX_POINTS_PER_LINE points plus thickness and pointSize (scriptTypes.hpp).
_RESOLVE_CAP = 2 * 200 + 2


class ScriptError(ValueError):
  """A script could not be read, parsed without errors, or applied."""


@dataclass(frozen=True)
class Diagnostic:
  severity: str
  code: str
  line: int
  col: int
  length: int
  message: str

  def format(self, label: str) -> str:
    """``file:line:col: severity: message [CODE]`` — what ``--script-check`` prints."""
    return "%s:%d:%d: %s" % (label, self.line, self.col, self.__tail())

  def dump_line(self) -> str:
    """``line:col:len: severity: message [CODE]`` — the fixture corpus spelling."""
    return "%d:%d:%d: %s" % (self.line, self.col, self.length, self.__tail())

  def __tail(self) -> str:
    return "%s: %s [%s]" % (self.severity, self.message, self.code)


@dataclass(frozen=True)
class Token:
  kind: str
  line: int
  col: int
  length: int


@dataclass(frozen=True)
class Block:
  index: int
  source: str
  kind: str
  frame: int
  op_start: int
  op_count: int


@dataclass(frozen=True)
class Op:
  index: int
  kind: str
  block: int
  edit_index: int
  line: int
  col: int
  strs: tuple = field(default_factory=tuple)
  toks: tuple = field(default_factory=tuple)
  nums: tuple = field(default_factory=tuple)

  def str_at(self, k: int) -> str:
    return self.strs[k] if 0 <= k < len(self.strs) else ""

  def num_at(self, k: int, default: float = 0.0) -> float:
    return self.nums[k] if 0 <= k < len(self.nums) else default


def _text(raw) -> str:
  return raw.decode("utf-8", "replace") if raw else ""


class Script:
  """One parsed ``.stc`` program. Use it as a context manager, or call :meth:`close`."""

  def __init__(self, handle: int, core: Core) -> None:
    self._handle = handle
    self._core = core
    lib = core._lib
    self.diagnostics: tuple = _read_diagnostics(lib, handle)
    self.tokens: tuple = _read_tokens(lib, handle)
    self.blocks: tuple = _read_blocks(lib, handle)
    self.ops: tuple = _read_ops(lib, handle)
    self.dump: str = _text(lib.stencil_cli_scriptDump(handle))

  @classmethod
  def parse(cls, text: str, core: (Core | NoneType) = None) -> "Script":
    """Parse ``text``; the returned program carries its own diagnostics."""
    core = core or get_core()
    raw = text.encode("utf-8")
    handle = core._lib.stencil_cli_scriptParse(raw, len(raw))
    if not handle: raise ScriptError("the core refused to parse that script")
    return cls(handle, core)

  def close(self) -> None:
    if self._handle:
      self._core._lib.stencil_cli_scriptDestroy(self._handle)
      self._handle = 0

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

  def block_ops(self, block: Block) -> tuple:
    """The ops belonging to ``block``, in order."""
    return self.ops[block.op_start:block.op_start + block.op_count]

  def resolve(self, index: int, image_w: float, image_h: float) -> list:
    """One op's length tokens in pixels against the CURRENT image size.

    A crop earlier in the block already moved the frame, so pass what you hold now.
    """
    if not self._handle: raise ScriptError("this script handle is closed")
    buf = (ctypes.c_double * _RESOLVE_CAP)()
    n = self._core._lib.stencil_cli_scriptOpResolve(
      self._handle, index, float(image_w), float(image_h), PX_PER_CM, PX_PER_CM,
      buf, _RESOLVE_CAP,
    )
    if n < 0: raise ScriptError("op %d resolves to nothing" % index)
    return [buf[i] for i in range(n)]


def parse_script(text: str, core: (Core | NoneType) = None) -> Script:
  """Parse ``.stc`` source into a :class:`Script`."""
  return Script.parse(text, core)


def _read_diagnostics(lib, handle: int) -> tuple:
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


def _read_tokens(lib, handle: int) -> tuple:
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


def _read_blocks(lib, handle: int) -> tuple:
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


def _read_ops(lib, handle: int) -> tuple:
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
