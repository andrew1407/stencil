from __future__ import annotations

"""The console's command table, and the ``/help`` listing generated from it.

A method becomes a command by wearing :func:`command`; the first name is the verb, the
rest are aliases. A declaration that carries ``usage`` also earns a ``/help`` entry, so
the listing cannot silently fall out of step with what the REPL actually dispatches.
"""

from typing import Callable

# Where a help entry's description starts, and the widest signature that still lets its
# description share the line (a longer one gets the next line, indented to the column).
_DESC_COL = 25
_MAX_INLINE_COL = 29
CommandTable = dict[str, Callable]
NamedCommands = list[tuple[str, Callable]]


class _Command:
  """One registered verb: its names and, when it is listed, its /help entry."""

  def __init__(self, names, usage, help_text):
    self.names = names
    self.usage = usage
    self.help = help_text


def command(*names: str, usage: str = "", help: str = "") -> Callable:
  """Register a REPL verb. ``names[0]`` is canonical, the rest are aliases.

  Pass ``usage`` (e.g. ``"/save [path]"``) plus ``help`` to give the command a
  ``/help`` entry; omit them for an alias-only or undocumented verb. A ``help`` with
  embedded newlines keeps its own line breaks, indented to the description column.
  """

  def wrap(fn):
    fn._command = _Command(names, usage, help)
    return fn

  return wrap


def _registered(cls) -> NamedCommands:
  """Every registered method on ``cls``, in mixin-declaration order then source order.

  ``cls``'s own commands come last, so the listing reads mixin by mixin and ends with
  the REPL's own /help and /exit.
  """
  found: NamedCommands = list()
  for klass in [k for k in cls.__mro__[1:] if k is not object] + [cls]:
    entries = [v for v in vars(klass).values() if hasattr(v, "_command")]
    entries.sort(key=lambda fn: fn.__code__.co_firstlineno)
    found.extend((klass.__name__, fn) for fn in entries)
  return found


def build_table(cls) -> CommandTable:
  """``{verb: method}`` for every name (and alias) registered on ``cls``."""
  table: CommandTable = dict()
  for _owner, fn in _registered(cls):
    for name in fn._command.names:
      if name in table:  # pragma: no cover - guards a duplicate registration
        raise AssertionError("/%s is registered twice" % name)
      table[name] = fn
  return table


def _entry(usage: str, help_text: str) -> str:
  """Render one listing entry: signature, then its description at the column."""
  sig = "  " + usage
  pad = " " * _DESC_COL
  lines = help_text.split("\n")
  if len(sig) + 2 <= _MAX_INLINE_COL:
    head = sig.ljust(max(_DESC_COL, len(sig) + 2)) + lines[0]
    rest = lines[1:]
  else:
    head, rest = sig, lines
  return "\n".join([head] + [pad + ln for ln in rest])


def build_help(cls) -> str:
  """The ``/help`` listing, mirroring the Zig REPL's command listing."""
  out = ["commands:"]
  for _owner, fn in _registered(cls):
    spec = fn._command
    if spec.usage:
      out.append(_entry(spec.usage, spec.help))
  return "\n".join(out)
