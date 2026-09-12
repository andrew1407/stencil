"""pystencil's test package.

Importing it puts the repo's ``pystencil/`` directory on ``sys.path`` once, so every
module below can ``import pystencil`` however the suite was started (``python3 -m
unittest discover -s tests`` from ``pystencil/``, ``-m unittest tests.test_x``, or
``-m unittest discover -s tests -p "bench_*.py"`` for the opt-in benchmarks).
"""

import sys
from pathlib import Path

_PKG_ROOT = Path(__file__).resolve().parent.parent
if str(_PKG_ROOT) not in sys.path:
    sys.path.insert(0, str(_PKG_ROOT))
