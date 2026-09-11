from __future__ import annotations

"""The REPL's output channel.

Every line the console prints goes through one object, so the stream and the
``error: ``/``note: `` severity prefixes live in a single place instead of being
threaded through every command method.
"""

from typing import TextIO

from .._severity import error_line, note_line


class Console:
    """Writes the console's human-readable lines (stderr, like the Zig REPL)."""

    def __init__(self, out: TextIO) -> None:
        self.out = out

    def say(self, msg: str) -> None:
        """Emit a human-readable line to the console channel (stderr)."""
        self.out.write(msg + "\n")

    def err(self, msg: str) -> None:
        """Say `msg` as an ``error: `` line — the command did not do what was asked."""
        self.say(error_line(msg, self.out))

    def note(self, msg: str) -> None:
        """Say `msg` as a ``note: `` line — it went ahead, with something worth saying."""
        self.say(note_line(msg, self.out))

    def report_wrote(self, path: str, w: int, h: int) -> None:
        """The canonical output-report line shared by /save and /prompt variants
        (the Zig CLI's ``wrote {path} ({w}x{h})`` stderr contract)."""
        self.say("wrote %s (%dx%d)" % (path, w, h))


def mask(secret: str) -> str:
    """Mask a credential for display: ``(none)`` when empty, else stars + last 4."""
    if not secret:
        return "(none)"
    return ("****" + secret[-4:]) if len(secret) > 4 else "****"
