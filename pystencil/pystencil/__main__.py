"""Package entry point so ``python -m pystencil`` runs the CLI.

Mirrors the ``stencil-py`` console-script entry point declared in pyproject; both
just delegate to :func:`pystencil.cli.main`.
"""

from __future__ import annotations

import sys

from .cli import main


if __name__ == "__main__": sys.exit(main())
