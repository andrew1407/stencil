"""The ctypes boundary: the shared library's signatures, and the marshalling either way.

``ctypes`` is imported here and by the bottom-layer modules that call through it — the
loader ``_native``, ``core`` and ``_raster/ops`` (``Core``), ``_script`` (``Script``) — and
nowhere above them; ``tests/test_layer_boundary.py`` pins the set.
"""

from __future__ import annotations
