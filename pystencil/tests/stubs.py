"""Shared test doubles for the LLM suite, plus the minimal-plan helper.

The stub editor/client/console record what execute_op_plan and Chat ask of them,
so most of the suite runs with no native core and no network.
"""

from __future__ import annotations

import json
import sys
from pathlib import Path

# Make the package importable when running `python3 -m unittest` from pystencil/.
_PKG_ROOT = Path(__file__).resolve().parent.parent
if str(_PKG_ROOT) not in sys.path:
    sys.path.insert(0, str(_PKG_ROOT))

from pystencil.llm import LLM_SYSTEM_PROMPT


def _plan_json(**kw) -> str:
    """A minimal valid plan JSON with overrides."""
    doc = {"version": 1, "reply": "ok", "actions": [], "variants": []}
    doc.update(kw)
    return json.dumps(doc)


class _StubEditor:
    """Records the Editor calls execute_op_plan makes (no native core needed).

    Variant branches are created as ``type(editor)()``, so this class also stands in
    for the per-variant editors; the class-level registry collects every instance.
    """

    instances: list = []

    def __init__(self) -> None:
        self.calls: list = []
        _StubEditor.instances.append(self)

    def _record(self, *call):
        self.calls.append(call)
        return self

    def load(self, src, **kw):
        return self._record("load", src, kw.get("name"))

    def crop(self, spec, album=False):
        return self._record("crop", spec, "album") if album else self._record("crop", spec)

    def rotate(self, quarters):
        return self._record("rotate", quarters)

    def set_filter(self, mode):
        return self._record("set_filter", mode)

    def set_filter_color(self, color):
        return self._record("set_filter_color", color)

    def draw(self, lines):
        return self._record("draw", lines)

    def set_formula(self, axis, expr):
        return self._record("set_formula", axis, expr)

    def set_allow_formulas(self, on):
        return self._record("set_allow_formulas", on)

    def set_page_format(self, name, width=None, height=None):
        if width is None:
            return self._record("set_page_format", name)
        return self._record("set_page_format", name, width, height)

    def blank(self, width=None, height=None, color="#ffffff", page="A4"):
        return self._record("blank", color, page)

    # §2 undo/redo: budgets say how many history entries each direction can walk
    # (0 by default — a fresh stub has nothing to step).
    undo_budget = 0
    redo_budget = 0

    def undo(self):
        self.calls.append(("undo",))
        if self.undo_budget > 0:
            self.undo_budget -= 1
            return True
        return False

    def redo(self):
        self.calls.append(("redo",))
        if self.redo_budget > 0:
            self.redo_budget -= 1
            return True
        return False

    def reset(self):
        return self._record("reset")

    def result(self):
        self.calls.append(("result",))
        return "img<%d calls>" % len(self.calls)


class _StubClient:
    """A canned LlmClient stand-in recording the messages it was asked to send."""

    def __init__(self, *replies: str) -> None:
        self.replies = list(replies)
        self.sent: list = []
        self.systems: list = []

    def chat(self, messages, system=LLM_SYSTEM_PROMPT):
        self.sent.append([dict(m) for m in messages])
        self.systems.append(system)
        return self.replies.pop(0) if len(self.replies) > 1 else self.replies[0]


class _SavingStubEditor(_StubEditor):
    """A stub that also stands in for the project-save path: it records the loads and
    WRITES each .stencil, so the " 2"/" 3" collision suffix is exercised for real."""

    def __init__(self) -> None:
        super().__init__()
        self.saved: list = []
        self.image = True
        self.name = "current"

    def has_image(self):
        return self.image

    def save_project(self, path):
        self.saved.append(path)
        with open(path, "w", encoding="utf-8") as fh:
            fh.write("{}")
        return path


class _StubConsole:
    """Records the §10 hook calls execute_op_plan makes, optionally missing."""

    def __init__(self, notes=None) -> None:
        self.notes = dict(notes or {})
        self.calls: list = []

    def _hook(self, op, action):
        self.calls.append((op, action))
        return self.notes.get(op)

    def plan_connect(self, action):
        return self._hook("connect", action)

    def plan_disconnect(self, action):
        return self._hook("disconnect", action)

    def plan_delete(self, action):
        return self._hook("delete", action)

    def plan_open_url(self, action):
        return self._hook("openUrl", action)

    def plan_clear(self, action):
        return self._hook("clear", action)

    def plan_clear_chat(self, action):
        return self._hook("clearChat", action)
