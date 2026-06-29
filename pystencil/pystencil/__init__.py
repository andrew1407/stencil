"""pystencil — a stdlib-only Python front-end over the shared Stencil C++ core.

Mirrors the browser editor's window.stencil facade and the Zig CLI's editing pipeline
against the SAME core/ logic via ctypes (no third-party deps). This module file is the
package marker; the public surface is re-exported here as the other modules land.
"""

from __future__ import annotations

__version__ = "0.1.0"

# Public surface: the ctypes core binding, the value types (Image / layout dataclasses),
# the chainable Editor facade (aliased as Stencil), the collaboration-server clients, and
# the pure-Python codecs module. All stdlib-only, no third-party deps.
from . import codecs
from .core import Core, get_core
from .image import Image
from .layout import Point, Line, Layout
from .editor import Editor
from .server import ServerConnection, ConnectionManager

# `Stencil` is the friendly alias for the Editor facade (mirrors window.stencil).
Stencil = Editor

__all__ = [
    "Core",
    "get_core",
    "Image",
    "Point",
    "Line",
    "Layout",
    "Editor",
    "Stencil",
    "ServerConnection",
    "ConnectionManager",
    "codecs",
    "__version__",
]
