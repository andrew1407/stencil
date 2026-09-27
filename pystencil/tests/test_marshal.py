"""The source-buffer marshalling aliases the caller's pixels instead of copying them, and
crop/rotate read every source kind the same."""

from __future__ import annotations

import ctypes
import unittest

from pystencil._ffi.marshal import _buf_view, _bytes_arg
from tests.helpers.nativecase import require_core


class SourceAliasTests(unittest.TestCase):
  def test_a_bytearray_source_is_aliased(self):
    buf = bytearray(b"\x01\x02\x03\x04")
    view = _bytes_arg(buf)
    buf[0] = 9
    self.assertEqual(view[0], 9)

  def test_a_bytes_source_passes_as_itself(self):
    # ...and a c_void_p parameter takes bytes as a pointer to their own buffer.
    as_string = ctypes.pythonapi.PyBytes_AsString
    as_string.restype = ctypes.c_void_p
    as_string.argtypes = [ctypes.py_object]
    src = bytes(range(16))
    self.assertIs(_bytes_arg(src), src)
    self.assertEqual(ctypes.cast(src, ctypes.c_void_p).value, as_string(src))

  def test_a_memoryview_source_is_copied_once_and_reads_the_same(self):
    self.assertEqual(bytes(_bytes_arg(memoryview(bytes(range(8))))), bytes(range(8)))

  def test_a_dropped_view_releases_the_bytearray(self):
    buf = bytearray(4)
    _buf_view(buf)
    buf.extend(b"\x00")
    self.assertEqual(len(buf), 5)


class SourceKindParityTests(unittest.TestCase):
  def setUp(self):
    self.core = require_core()
    self.pixels = bytes((i * 7) & 0xFF for i in range(3 * 2 * 4))

  def test_crop_and_rotate_read_bytes_and_bytearray_alike(self):
    for src in (self.pixels, bytearray(self.pixels), memoryview(self.pixels)):
      with self.subTest(kind=type(src).__name__):
        self.assertEqual(
          self.core.crop_image_rgba(src, 3, 2, 1, 0, 2, 2),
          self.core.crop_image_rgba(self.pixels, 3, 2, 1, 0, 2, 2))
        self.assertEqual(
          self.core.rotate_image_rgba(src, 3, 2, 1),
          self.core.rotate_image_rgba(self.pixels, 3, 2, 1))

  def test_every_buffer_is_resizable_once_its_call_returns(self):
    # BufferError here means a view outlived its call (a ctypes.cast cycle waits for GC).
    src = bytearray(self.pixels)
    self.core.crop_image_rgba(src, 3, 2, 0, 0, 1, 1)
    self.core.rotate_image_rgba(src, 3, 2, 1)
    self.core.apply_filter("bw", src, 6)
    self.core.apply_contour(src, 3, 2)
    self.core.fill_rgba(src, 6, 1, 2, 3, 4)
    self.core.rasterize_line(src, 3, 2, [(0.0, 0.0), (2.0, 1.0)])
    src.extend(b"\x00")
    self.assertEqual(len(src), len(self.pixels) + 1)


if __name__ == "__main__":
  unittest.main()
