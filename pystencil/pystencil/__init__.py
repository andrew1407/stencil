"""pystencil — a stdlib-only Python front-end over the shared Stencil C++ core, through
ctypes: the browser's ``window.stencil`` facade and the Zig CLI's pipeline over the same
``core/``. The public surface is re-exported here.
"""

from __future__ import annotations

__version__ = "0.1.0"

from . import _native

_native.sync_data()

from . import codecs
from ._ffi.formula import FormulaContext
from .core import Core, get_core
from .image import Image
from .layout import Point, Line, Layout
from .editor import Editor, ScriptResult
from .server import ServerConnection, ConnectionManager, diff_projects
from .sitesource import MediaItem, scan_page, download_media
from .script import (
  Script,
  ScriptError,
  ScriptRun,
  parse_script,
  run_script,
  run_script_text,
)
from .llm import (
  LLM_SYSTEM_PROMPT,
  Chat,
  LlmClient,
  LlmConfig,
  LlmError,
  LlmExecutionError,
  LlmPlanError,
  OpPlan,
  Variant,
  execute_op_plan,
  parse_op_plan,
)

Stencil = Editor

__all__ = [
  "Core",
  "FormulaContext",
  "get_core",
  "Image",
  "Point",
  "Line",
  "Layout",
  "Editor",
  "Stencil",
  "Script",
  "ScriptError",
  "ScriptResult",
  "ScriptRun",
  "parse_script",
  "run_script",
  "run_script_text",
  "ServerConnection",
  "ConnectionManager",
  "diff_projects",
  "MediaItem",
  "scan_page",
  "download_media",
  "LLM_SYSTEM_PROMPT",
  "Chat",
  "LlmClient",
  "LlmConfig",
  "LlmError",
  "LlmExecutionError",
  "LlmPlanError",
  "OpPlan",
  "Variant",
  "execute_op_plan",
  "parse_op_plan",
  "codecs",
  "__version__",
]
