"""The single gate on the compiled core, and the base case that sits behind it.

Every suite that needs the shared library goes through :func:`require_core` rather than
re-deriving the try/SkipTest dance, so ``STENCIL_SKIP_NATIVE=1`` turns the whole native
leg off in one place — the suite then runs on a machine with no C++ compiler.
"""

from __future__ import annotations

import os
import unittest

SKIP_NATIVE_ENV = "STENCIL_SKIP_NATIVE"


def require_core():
    """The loaded core binding, or ``SkipTest`` — no compiler, or the env opt-out.

    Builds the library on demand (``build.py`` via ``get_core``).
    """
    if os.environ.get(SKIP_NATIVE_ENV, "") not in ("", "0"):
        raise unittest.SkipTest("%s is set: skipping the native core" % SKIP_NATIVE_ENV)
    try:
        from pystencil.core import get_core

        return get_core()
    except Exception as exc:  # any failure to load/compile means "no native lib"
        raise unittest.SkipTest("native core unavailable: %s" % exc)


class NativeCase(unittest.TestCase):
    """A case whose every test needs the compiled core; ``cls.core`` is the binding."""

    @classmethod
    def setUpClass(cls) -> None:
        cls.core = require_core()
