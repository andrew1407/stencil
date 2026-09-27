"""ctypes rows and calls for the stb codec shim (``pystencil/stb/shim.c``) in the native lib.

Unlike the core ABI, the shim hands back heap memory: the decoded plane and the encoded
bytes are malloc'd in C, copied into Python here and freed in the same call, so nothing
C-owned outlives it. Failures come back as ``ValueError`` carrying stb's reason.
"""

from __future__ import annotations

import ctypes

from .marshal import _buf_view, _bytes_arg, _check_dims

_intp = ctypes.POINTER(ctypes.c_int)


def bind_stb(lib: ctypes.CDLL) -> None:
  """Set .argtypes/.restype on the shim's entry points; a lib without them raises
  ``AttributeError``."""
  lib.stencil_py_decode_rgba.restype = ctypes.c_void_p
  lib.stencil_py_decode_rgba.argtypes = [
    ctypes.c_void_p, ctypes.c_int, ctypes.c_size_t, _intp, _intp, ctypes.POINTER(ctypes.c_char_p)]

  lib.stencil_py_encode_jpeg.restype = ctypes.c_void_p
  lib.stencil_py_encode_jpeg.argtypes = [ctypes.c_void_p] + [ctypes.c_int] * 3 + [_intp]

  lib.stencil_py_free.restype = None
  lib.stencil_py_free.argtypes = [ctypes.c_void_p]


def decode_image(lib: ctypes.CDLL, data, max_block: int = 0) -> tuple[int, int, bytearray]:
  """``(width, height, rgba8)`` for encoded ``data`` — whatever the decoder unit reads — with
  no block stb allocates past ``max_block`` bytes (0 = no cap)."""
  width, height = ctypes.c_int(), ctypes.c_int()
  error = ctypes.c_char_p()
  cap = min(max_block, ctypes.c_size_t(-1).value)
  ptr = lib.stencil_py_decode_rgba(_bytes_arg(data), len(data), cap, ctypes.byref(width),
                                   ctypes.byref(height), ctypes.byref(error))
  if not ptr:
    raise ValueError((error.value or b"unknown error").decode("ascii", "replace"))
  try:
    out = bytearray(width.value * height.value * 4)
    ctypes.memmove(_buf_view(out), ptr, len(out))
  finally:
    lib.stencil_py_free(ptr)
  return width.value, height.value, out


def encode_jpeg(lib: ctypes.CDLL, width: int, height: int, data, quality: int) -> bytes:
  """RGBA8 ``data`` as JPEG bytes at ``quality`` (1–100); the alpha channel is dropped."""
  if width <= 0 or height <= 0:
    raise ValueError("cannot encode a %dx%d image" % (width, height))
  _check_dims(data, width, height, "encode_jpeg")
  size = ctypes.c_int()
  ptr = lib.stencil_py_encode_jpeg(_bytes_arg(data), width, height, quality, ctypes.byref(size))
  if not ptr:
    raise ValueError("stb refused to encode a %dx%d JPEG" % (width, height))
  try:
    return ctypes.string_at(ptr, size.value)
  finally:
    lib.stencil_py_free(ptr)
