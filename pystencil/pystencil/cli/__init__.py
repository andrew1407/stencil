"""Command-line front-end for pystencil — ``python -m pystencil`` / ``stencil-py``.

The Python counterpart of the Zig CLI: the one-shot pipeline (source → crop → flip → rotate →
filter → draw-layout) and the ``--console`` REPL, both over the shared core and printing the
CLI's stderr grammar (``cli/CONTRACT.md``). This module is the façade over the split modules.
"""

from __future__ import annotations

import sys
from typing import Sequence

from .._ffi.types import NoneType
from .. import codecs
from .._severity import emit_error
from ..server import ServerError
from .blank import BlankSpec
from .console import Console, mask as _mask
from .hooks import PlanConsole
from .oneshot import _build_parser, _resolve_output, _run_pipeline, _run_scrape
from .script import run_mode as _run_script_mode
from .commands.prompt import _load_only_plan
from .registry import command
from .repl import _HELP, _Repl, _parse_command

def main(argv: (Sequence[str] | NoneType) = None) -> int:
  """The process exit code (0 on success); every human-facing line goes to stderr."""
  parser = _build_parser()
  args = parser.parse_args(list(argv) if argv is not None else None)
  err = sys.stderr

  if args.console: return _Repl(err).run(sys.stdin)

  try:
    if args.script or args.script_check or args.script_plan:
      return _run_script_mode(args, err)
    if args.source_site is not None: return _run_scrape(args, err)
    return _run_pipeline(args, err)
  except ServerError as e:
    emit_error(err, "%s" % e)
    return 1
  except (ValueError, RuntimeError, OSError, codecs.CodecError) as e:
    emit_error(err, "%s" % e)
    return 1

