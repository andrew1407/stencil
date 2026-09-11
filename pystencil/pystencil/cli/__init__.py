from __future__ import annotations

"""Command-line front-end for pystencil — ``python -m pystencil`` / ``stencil-py``.

This is the Python counterpart of the Zig CLI (``cli/``). It offers the same two
modes over the shared core, driving an :class:`~pystencil.editor.Editor`:

* **One-shot pipeline** (default): flags mirror the Zig CLI as far as is
  practical — ``-i/--input``, ``--blank``, ``-f/--frame``, ``-c/--crop``,
  ``--album``, ``-r/--rotate``, ``-l/--layout`` (draw), ``--filter``,
  ``--save-layout`` (export the structured layout), and a positional output
  image. Steps run in the fixed order **source → crop → rotate → filter →
  draw-layout**, then the image and/or layout are written. On success the canonical
  ``wrote {path} ({w}x{h})`` line is printed to stderr (matching the Zig
  contract); failures print ``error: …`` and return a non-zero code.

* **Console / REPL** (``--console`` / ``--repl``): reads ``/command <args>``
  lines mirroring the Zig console grammar (``cli/src/console/commands.zig``):
  ``/upload`` (``/open``/``/load``), ``/blank`` (``/new``), ``/format``,
  ``/crop``, ``/rotate`` (``/rot``), ``/filter`` (+ ``/bw`` ``/sepia``
  ``/none``), ``/apply`` (``/draw``), ``/layout`` (export), ``/save``,
  ``/undo`` ``/redo`` ``/reset``, ``/status``, ``/connect``, ``/disconnect``,
  ``/delete`` (``/rm``), ``/projects``, ``/fetch``, ``/prompt`` (``/p``) +
  ``/llm`` + ``/chat`` (the LLM assistant — see ``llm-contract.md``),
  ``/help``, ``/exit``. Messages go to stderr, the
  CLI's human channel, exactly like the Zig REPL.

Both modes reuse the exact same ``core/`` transforms as the browser and Zig

Split across blank / oneshot / console / registry / hooks / repl and the per-area
``commands/`` modules; this module is the façade that keeps ``main`` and the console's
names where callers expect them.
"""

import sys
from typing import Optional, Sequence

from .. import codecs
from .._severity import emit_error
from ..server import ServerError
from .blank import BlankSpec
from .console import Console, mask as _mask
from .hooks import PlanConsole
from .oneshot import _build_parser, _resolve_output, _run_pipeline, _run_scrape
from .commands.prompt import _load_only_plan
from .registry import command
from .repl import _HELP, _Repl, _parse_command

def main(argv: Optional[Sequence[str]] = None) -> int:
    """Entry point for ``python -m pystencil`` and the ``stencil-py`` script.

    ``argv`` defaults to ``sys.argv[1:]``. Returns a process exit code (0 on
    success, non-zero on error). All human-facing output goes to **stderr** so
    that any piped/automation use stays clean.
    """
    parser = _build_parser()
    args = parser.parse_args(list(argv) if argv is not None else None)
    err = sys.stderr

    if args.console:
        return _Repl(err).run(sys.stdin)

    try:
        if args.source_site is not None:
            return _run_scrape(args, err)
        return _run_pipeline(args, err)
    except ServerError as e:
        emit_error(err, "%s" % e)
        return 1
    except (ValueError, RuntimeError, OSError, codecs.CodecError) as e:
        emit_error(err, "%s" % e)
        return 1

