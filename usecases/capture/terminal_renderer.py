#!/usr/bin/env python3
"""A small terminal emulator that turns an SGR-coloured text stream (the CLI's pinned
goldens) into an HTML page: cursor placement, erase, SGR with truecolor, OSC skipped.
Stdlib only. Used by cli_listings.py.
"""

from __future__ import annotations

import codecs
import html
import sys
from functools import partial

from terminal_containers import (BASIC_COLORS, CSS, DEFAULT_BG, DEFAULT_COLS, DEFAULT_FG, DEFAULT_TITLE,
                                 WINDOW_DOTS, Cell, CsiCommand, NoneType, Style, color256)


class TerminalEmulator:
  """Feeds bytes in, renders HTML out. `rows=None` keeps a growing scrollback."""

  @staticmethod
  def __numbers(params: str) -> list[int]:
    return [int(x) if x else 0 for x in params.split(";")] if params else list()

  @staticmethod
  def __span(style: tuple, text: str) -> str:
    fg, bg, bold, dim = style or (None, None, False, False)
    css = ";".join(s for s in (fg and "color:%s" % fg, bg and "background:%s" % bg, bold and "font-weight:700",
                               dim and "opacity:.6") if s)
    esc = html.escape(text)
    return '<span style="%s">%s</span>' % (css, esc) if css else esc

  def __init__(self, rows: (int | NoneType) = None, cols: int = DEFAULT_COLS):
    self.rows, self.cols = rows, cols
    self.grid: list[list[Cell]] = list()
    self.row = self.col = 0
    self.wrap = True
    self.style = Style()
    self.__decoder = codecs.getincrementaldecoder("utf-8")("replace")
    self.__state, self.__buf = "ground", ""
    self.__csi_handlers = self.__make_csi_handlers()
    self.__sgr_handlers = self.__make_sgr_handlers()
    self.__ground_handlers = {
      "\x1b": self.__enter_escape, "\n": self.__line_feed, "\r": self.__carriage_return,
      "\b": self.__backspace, "\t": self.__tab,
    }
    self.__states = {"ground": self.__on_ground, "esc": self.__on_escape, "skip1": self.__on_skip,
                     "csi": self.__on_csi, "osc": self.__on_osc}

  # ── input ────────────────────────────────────────────────────────────────────
  def feed(self, data: bytes) -> None:
    for ch in self.__decoder.decode(data):
      self.__states[self.__state](ch)

  def to_html(self, title: str) -> str:
    out = list()
    rows = self.rows or len(self.grid)
    for r in range(rows):
      out.append(self.__row_html(r))
      out.append("\n")
    dots = "".join('<i style="background:%s"></i>' % c for c in WINDOW_DOTS)
    return ('<!doctype html><meta charset="utf-8"><style>%s</style><div class="term"><div class="bar">%s<b>%s</b></div>'
            '<pre>%s</pre></div>' % (CSS, dots, html.escape(title), "".join(out)))

  # ── the character states ─────────────────────────────────────────────────────
  def __on_ground(self, ch: str) -> None:
    handler = self.__ground_handlers.get(ch)
    if handler: handler()
    elif ch >= " ": self.__put(ch)

  def __on_escape(self, ch: str) -> None:
    if ch == "[": self.__state, self.__buf = "csi", ""
    elif ch == "]": self.__state, self.__buf = "osc", ""
    elif ch in "()#%": self.__state = "skip1"
    else: self.__state = "ground"

  def __on_skip(self, _ch: str) -> None:
    self.__state = "ground"

  def __on_csi(self, ch: str) -> None:
    self.__buf += ch
    if "\x40" <= ch <= "\x7e":
      self.__csi(self.__buf)
      self.__state = "ground"

  def __on_osc(self, ch: str) -> None:
    if ch == "\x07" or (ch == "\\" and self.__buf.endswith("\x1b")): self.__state = "ground"
    else: self.__buf += ch

  def __enter_escape(self) -> None: self.__state = "esc"
  def __line_feed(self) -> None:
    self.row += 1
    self.col = 0 if self.rows is None else self.col
  def __carriage_return(self) -> None: self.col = 0
  def __backspace(self) -> None: self.col = max(0, self.col - 1)
  def __tab(self) -> None: self.col = (self.col // 8 + 1) * 8

  # ── the grid ─────────────────────────────────────────────────────────────────
  def __line(self, r: int) -> list[Cell]:
    while len(self.grid) <= r: self.grid.append(list())
    return self.grid[r]

  def __put(self, ch: str) -> None:
    if self.col >= self.cols:
      if not self.wrap: return
      self.row, self.col = self.row + 1, 0
    self.__scroll_into_view()
    line = self.__line(self.row)
    while len(line) <= self.col: line.append(Cell())
    line[self.col] = Cell.of(ch, self.style)
    self.col += 1

  def __scroll_into_view(self) -> None:
    if self.rows and self.row >= self.rows:
      del self.grid[:self.row - self.rows + 1]
      self.row = self.rows - 1

  def __erase_line(self, mode: int) -> None:
    line = self.__line(self.row)
    if mode == 0: del line[self.col:]
    elif mode == 1:
      for i in range(min(self.col + 1, len(line))): line[i] = Cell()
    else: line.clear()

  def __erase_display(self, mode: int) -> None:
    if mode == 0:
      self.__erase_line(0)
      del self.grid[self.row + 1:]
    else:
      self.grid = list()

  def __row_html(self, r: int) -> str:
    line = self.grid[r] if r < len(self.grid) else list()
    cells = line + [Cell()] * (self.cols - len(line))
    out, run, style = list(), list(), None
    for cell in cells[:self.cols]:
      fg, bg = (cell.bg or DEFAULT_BG, cell.fg or DEFAULT_FG) if cell.rev else (cell.fg, cell.bg)
      st = (fg, bg, cell.bold, cell.dim)
      if st != style and run:
        out.append(self.__span(style, "".join(run)))
        run = list()
      style = st
      run.append(cell.ch)
    out.append(self.__span(style, "".join(run)))
    return "".join(out)

  # ── SGR and CSI, as tables ───────────────────────────────────────────────────
  def __set(self, field: str, value) -> None:
    setattr(self.style, field, value)

  def __make_sgr_handlers(self) -> dict:
    table = {
      0: self.style.reset, 1: partial(self.__set, "bold", True), 2: partial(self.__set, "dim", True),
      7: partial(self.__set, "rev", True), 27: partial(self.__set, "rev", False),
      39: partial(self.__set, "fg", None), 49: partial(self.__set, "bg", None),
      22: lambda: (self.__set("bold", False), self.__set("dim", False)),
    }
    for i in range(8):
      table[30 + i] = partial(self.__set, "fg", BASIC_COLORS[i])
      table[90 + i] = partial(self.__set, "fg", BASIC_COLORS[i + 8])
      table[40 + i] = partial(self.__set, "bg", BASIC_COLORS[i])
      table[100 + i] = partial(self.__set, "bg", BASIC_COLORS[i + 8])
    return table

  def __sgr(self, params: str) -> None:
    codes = self.__numbers(params) or [0]
    i = 0
    while i < len(codes):
      n = codes[i]
      if n in (38, 48) and i + 1 < len(codes):
        color, used = self.__extended_color(codes, i)
        self.__set("fg" if n == 38 else "bg", color)
        i += used
      else:
        handler = self.__sgr_handlers.get(n)
        if handler: handler()
      i += 1

  # 38/48 take either `2;r;g;b` (truecolor) or `5;n` (256-colour); returns (colour, consumed).
  def __extended_color(self, codes: list[int], i: int) -> tuple:
    if codes[i + 1] == 2 and i + 4 < len(codes):
      return "#%02x%02x%02x" % tuple(min(255, v) for v in codes[i + 2:i + 5]), 4
    if codes[i + 1] == 5 and i + 2 < len(codes):
      return color256(codes[i + 2]), 2
    return None, 0

  def __make_csi_handlers(self) -> dict:
    def goto(cmd): self.row, self.col = max(cmd.first, 1) - 1, cmd.at(1) - 1
    def up(cmd): self.row = max(0, self.row - max(cmd.first, 1))
    def down(cmd): self.row += max(cmd.first, 1)
    def right(cmd): self.col += max(cmd.first, 1)
    def left(cmd): self.col = max(0, self.col - max(cmd.first, 1))
    def column(cmd): self.col = max(cmd.first, 1) - 1
    def row(cmd): self.row = max(cmd.first, 1) - 1
    def modes(cmd):
      if not cmd.private: return
      if 7 in cmd.nums: self.wrap = cmd.final == "h"
      if 1049 in cmd.nums: self.grid, self.row, self.col = list(), 0, 0
    return {
      "H": goto, "f": goto, "A": up, "B": down, "C": right, "D": left, "G": column, "d": row,
      "J": lambda cmd: self.__erase_display(cmd.first), "K": lambda cmd: self.__erase_line(cmd.first),
      "m": lambda cmd: self.__sgr(cmd.params), "h": modes, "l": modes,
    }

  def __csi(self, seq: str) -> None:
    body, final = seq[:-1], seq[-1]
    params = body.lstrip("?")
    cmd = CsiCommand(final, params, self.__numbers(params), body.startswith("?"))
    handler = self.__csi_handlers.get(final)
    if handler: handler(cmd)


def render(ansi_path: str, out_html: str, rows: (int | NoneType), cols: int, title: str) -> str:
  term = TerminalEmulator(rows, cols)
  with open(ansi_path, "rb") as src: term.feed(src.read())
  with open(out_html, "w") as dst: dst.write(term.to_html(title))
  return out_html


def main(argv: list[str]) -> None:
  src, dst = argv[1], argv[2]
  opts = dict(zip(argv[3::2], argv[4::2]))
  rows = int(opts["--rows"]) if "--rows" in opts else None
  render(src, dst, rows, int(opts.get("--cols", str(DEFAULT_COLS))), opts.get("--title", DEFAULT_TITLE))


if __name__ == "__main__":
  main(sys.argv)
