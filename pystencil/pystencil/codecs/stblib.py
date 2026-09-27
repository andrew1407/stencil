"""The native library with the stb shim bound (``pystencil/stb/shim.c``), shared by the JPEG
and PNG codecs: loaded, and built on demand, once per process. A library that cannot be had,
or was built without the pinned stb headers, is remembered as missing, with its reason.
"""

from __future__ import annotations

import threading

from .._ffi.stb import bind_stb, decode_image
from .._native import load_library
from .sniff import CodecError, check_size

# Headroom over the largest plane: stb's fixed blocks (the JPEG decoder state is ~20 KiB).
SLACK = 1 << 16

_LOCK = threading.Lock()
_LIB = None
_MISSING = None


def loaded():
  """The bound library, or None when this process has none with the stb units."""
  global _LIB, _MISSING
  with _LOCK:
    if _LIB is None and _MISSING is None:
      try:
        lib = load_library()
        bind_stb(lib)
        _LIB = lib
      except (OSError, RuntimeError) as exc:
        _MISSING = "needs pystencil's native library: %s" % exc
      except AttributeError:
        _MISSING = ("is not built: the native library has no stb decoder, whose pinned headers "
                    "come from github.com/nothings/stb — run `python3 build.py` online once")
    return _LIB


def library(fmt: str):
  """The bound library, or ``CodecError`` saying why ``fmt`` cannot be coded without it."""
  lib = loaded()
  if lib is None: raise CodecError("%s %s" % (fmt, _MISSING))
  return lib


def block_cap(data: bytes, width: int, height: int, rows: int = 0) -> int:
  """The largest block stb may take decoding ``data`` (twin of the CLI's ``decodeGuard``): its
  own input, a 16-bit RGBA plane padded as a JPEG pads its MCUs, or twice a PNG's inflated
  ``rows``, since stb grows an inflate by doubling."""
  return max(2 * len(data) + 4096, 8 * (width + 32) * (height + 32), 2 * (rows + 8 * height)) + SLACK


def decode(fmt: str, lib, data: bytes, cap: int) -> tuple[int, int, bytearray]:
  """``data`` decoded by stb under ``cap``, its failure a ``CodecError`` naming ``fmt``."""
  try:
    width, height, pixels = decode_image(lib, data, cap)
  except ValueError as exc:
    raise CodecError("%s decode failed: %s" % (fmt, exc)) from None
  check_size(width, height, fmt)
  return width, height, pixels
