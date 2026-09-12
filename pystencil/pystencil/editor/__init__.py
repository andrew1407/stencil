from __future__ import annotations

"""The chainable :class:`Editor` facade, split into collaborating mixins.

``editor`` holds the history (original image, snapshot stack, cursor); the rest of the
surface lives in the modules it composes. This module re-exports what callers bind to.
"""

from ._snapshot import _A4_FALLBACK, LayoutLike, LoadSource, _Snapshot
from .editor import Editor
from .project import _valid_chat_doc

__all__ = ["Editor", "LayoutLike", "LoadSource"]
