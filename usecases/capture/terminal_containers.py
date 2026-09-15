#!/usr/bin/env python3
"""The pieces terminal_renderer.py paints with: the palette and page CSS, one painted cell,
the live SGR style and a parsed CSI sequence. Stdlib only.
"""

from __future__ import annotations

from dataclasses import dataclass

NoneType = type(None)   # `types` grew it in 3.10; these scripts run on the system python

DEFAULT_FG = "#d8dee9"
DEFAULT_BG = "#15171e"
BASIC_COLORS = ("#1c1f26", "#e06c75", "#98c379", "#e5c07b", "#61afef", "#c678dd", "#56b6c2", "#d8dee9",
                "#5c6370", "#ff7b86", "#b5e890", "#ffd58a", "#82c4ff", "#dd9cf5", "#7fd6e0", "#ffffff")
WINDOW_DOTS = ("#ff5f57", "#febc2e", "#28c840")
DEFAULT_COLS = 120
DEFAULT_TITLE = "stencil"
CSS = ("body{margin:0;background:transparent}"
       ".term{display:inline-block;background:%s;border-radius:10px;padding:10px 14px 14px;"
       "box-shadow:0 12px 40px rgba(0,0,0,.45);font:14px/1.3 'JetBrains Mono',Menlo,'SF Mono',monospace}"
       ".bar{display:flex;gap:7px;align-items:center;margin:0 0 8px}.bar i{width:11px;height:11px;border-radius:50%%;display:inline-block}"
       ".bar b{margin-left:8px;color:#6d7385;font-weight:400;font-size:12px}"
       "pre{margin:0;color:%s;white-space:pre}pre span{white-space:pre}" % (DEFAULT_BG, DEFAULT_FG))


@dataclass
class Style:
  """The live SGR state: colours plus the three attributes the goldens use."""
  fg: (str | NoneType) = None
  bg: (str | NoneType) = None
  bold: bool = False
  dim: bool = False
  rev: bool = False

  def reset(self) -> None:
    self.fg = self.bg = None
    self.bold = self.dim = self.rev = False


@dataclass
class Cell:
  """One painted character with the style that was live when it landed."""
  ch: str = " "
  fg: (str | NoneType) = None
  bg: (str | NoneType) = None
  bold: bool = False
  dim: bool = False
  rev: bool = False

  @staticmethod
  def of(ch: str, style: Style) -> Cell:
    return Cell(ch, style.fg, style.bg, style.bold, style.dim, style.rev)


@dataclass
class CsiCommand:
  """A parsed CSI sequence: its final byte, raw parameters and their numbers."""
  final: str
  params: str
  nums: list[int]
  private: bool

  @property
  def first(self) -> int:
    return self.nums[0] if self.nums else 0

  def at(self, index: int) -> int:
    return self.nums[index] if len(self.nums) > index and self.nums[index] else 1


def color256(n: int) -> str:
  if n < 16:
    return BASIC_COLORS[n]
  if n < 232:
    n -= 16
    r, g, b = n // 36, (n // 6) % 6, n % 6
    return "#%02x%02x%02x" % tuple(0 if v == 0 else 55 + v * 40 for v in (r, g, b))
  v = 8 + (n - 232) * 10
  return "#%02x%02x%02x" % (v, v, v)
