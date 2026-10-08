"""The chainable :class:`Editor` facade over per-feature mixins; re-exports what callers bind to."""

from __future__ import annotations

from ._snapshot import _A4_FALLBACK, LayoutLike, LoadSource, _Snapshot
from .editor import Editor
from .project import _valid_chat_doc
from .script import ScriptResult

__all__ = ["Editor", "LayoutLike", "LoadSource", "ScriptResult"]
