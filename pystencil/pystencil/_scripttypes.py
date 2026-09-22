"""The value types a parsed ``.stc`` program hands back: diagnostics, tokens, blocks, ops.

Frozen dataclasses plus the ABI's own kind tables — the Python side of
``core/script/types.hpp``, carrying no handle and no ctypes.
"""

from __future__ import annotations

from dataclasses import dataclass, field

# Index order is the ABI's; never reorder, only append (types.hpp).
TOKEN_KINDS = tuple(
  "comment directive keyword number unit color string param punct ident error".split())
SOURCE_KINDS = tuple("project file url dir glob".split())
OP_KINDS = tuple("open frame crop filter line rect layout save undo redo".split())

Strings = tuple[str, ...]
Numbers = tuple[float, ...]
Pixels = list[float]


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

  def console_line(self, label: str) -> str:
    """``file:line:col: message [CODE]`` — the console spelling, whose severity the
    ``error: ``/``note: `` prefix of the line already carries."""
    return "%s:%d:%d: %s [%s]" % (label, self.line, self.col, self.message, self.code)

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
  strs: Strings = field(default_factory=tuple)
  toks: Strings = field(default_factory=tuple)
  nums: Numbers = field(default_factory=tuple)

  def str_at(self, k: int) -> str:
    return self.strs[k] if 0 <= k < len(self.strs) else ""

  def num_at(self, k: int, default: float = 0.0) -> float:
    return self.nums[k] if 0 <= k < len(self.nums) else default


Diagnostics = tuple[Diagnostic, ...]
Tokens = tuple[Token, ...]
Blocks = tuple[Block, ...]
Ops = tuple[Op, ...]
