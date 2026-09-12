from __future__ import annotations

"""Tests for the ``Image`` value type: construction, pixel count, blank, copy.

``Image`` is the buffer every other layer hands around: the codecs fill it, the
native core mutates it in place over ctypes, and the editor saves it. Two of its
guarantees are load-bearing and easy to break silently:

* the buffer is ALWAYS a ``bytearray`` — a ``bytes`` would make the core's
 in-place ops (``ctypes.from_buffer``) fail on a read-only object;
* ``blank`` works with or without a compiled core lib, producing identical
 pixels either way, so a codec-only checkout still runs.

Everything here is pure Python except the native path in ``test_blank_*``, which
is exercised both ways. The codec and disk round-trips live in test_image_io.py.
"""

import unittest
from unittest import mock

from pystencil.image import Image


class TestConstruction(unittest.TestCase):
  """Size validation and the bytearray guarantee."""

  def test_accepts_a_correctly_sized_buffer(self):
    img = Image(2, 3, bytearray(2 * 3 * 4))
    self.assertEqual(img.width, 2)
    self.assertEqual(img.height, 3)
    self.assertEqual(len(img.data), 24)

  def test_rejects_a_short_buffer(self):
    with self.assertRaises(ValueError) as ctx:
      Image(2, 2, bytearray(15))
    # The message names both numbers so a mismatch is diagnosable.
    self.assertIn("15", str(ctx.exception))
    self.assertIn("16", str(ctx.exception))

  def test_rejects_a_long_buffer(self):
    with self.assertRaises(ValueError):
      Image(2, 2, bytearray(17))

  def test_zero_sized_image_is_legal(self):
    img = Image(0, 0, bytearray())
    self.assertEqual(img.pixel_count, 0)
    self.assertEqual(len(img.data), 0)

  def test_bytes_are_coerced_to_bytearray(self):
    """The core mutates ``data`` in place via ctypes, which needs a writable buffer."""
    img = Image(1, 1, bytes(4))
    self.assertIsInstance(img.data, bytearray)
    img.data[0] = 9  # would raise on a `bytes`
    self.assertEqual(img.data[0], 9)

  def test_an_existing_bytearray_is_adopted_not_copied(self):
    """In-place core ops must write through to the caller's buffer."""
    buf = bytearray(4)
    img = Image(1, 1, buf)
    self.assertIs(img.data, buf)
    img.data[1] = 7
    self.assertEqual(buf[1], 7)


class TestPixelCount(unittest.TestCase):
  """``pixel_count`` is the unit the core ABI counts in, not bytes."""

  def test_counts_pixels_not_bytes(self):
    self.assertEqual(Image(4, 5, bytearray(80)).pixel_count, 20)

  def test_zero(self):
    self.assertEqual(Image(0, 7, bytearray()).pixel_count, 0)


class TestBlank(unittest.TestCase):
  """Solid-colour construction, via the native core and via the fallback."""

  def test_defaults_to_opaque_white(self):
    img = Image.blank(2, 2)
    self.assertEqual(bytes(img.data), bytes([255, 255, 255, 255] * 4))

  def test_custom_rgba_is_written_in_rgba_order(self):
    img = Image.blank(2, 1, (1, 2, 3, 4))
    self.assertEqual(bytes(img.data), bytes([1, 2, 3, 4, 1, 2, 3, 4]))

  def test_dimensions_are_recorded(self):
    img = Image.blank(3, 5, (0, 0, 0, 255))
    self.assertEqual((img.width, img.height), (3, 5))
    self.assertEqual(len(img.data), 3 * 5 * 4)

  def test_zero_sized_blank(self):
    img = Image.blank(0, 0)
    self.assertEqual(len(img.data), 0)

  def test_falls_back_to_pure_python_without_a_core_lib(self):
    """A codec-only checkout (no compiled core) must still build blank images."""
    with mock.patch("pystencil.core.get_core", side_effect=OSError("no core lib")) as m:
      img = Image.blank(2, 2, (10, 20, 30, 40))
    # Without this the test would pass vacuously if the patch ever stopped
    # intercepting (blank() resolves get_core lazily, inside the call).
    self.assertEqual(m.call_count, 1, "the native path was never attempted")
    self.assertEqual(bytes(img.data), bytes([10, 20, 30, 40] * 4))

  def test_the_two_paths_produce_identical_pixels(self):
    """The fallback is a reference implementation of the core's ``fill_rgba``."""
    native = Image.blank(5, 3, (7, 90, 200, 128))
    with mock.patch("pystencil.core.get_core", side_effect=OSError("no core lib")) as m:
      fallback = Image.blank(5, 3, (7, 90, 200, 128))
    self.assertEqual(m.call_count, 1, "the native path was never attempted")
    self.assertEqual(bytes(native.data), bytes(fallback.data))


class TestCopy(unittest.TestCase):
  """``copy`` must be deep in the buffer — core ops mutate in place."""

  def test_copy_matches_the_original(self):
    img = Image.blank(2, 2, (1, 2, 3, 4))
    clone = img.copy()
    self.assertEqual((clone.width, clone.height), (img.width, img.height))
    self.assertEqual(bytes(clone.data), bytes(img.data))

  def test_the_copy_has_its_own_buffer(self):
    img = Image.blank(2, 2, (1, 2, 3, 4))
    clone = img.copy()
    clone.data[0] = 200
    self.assertEqual(img.data[0], 1, "mutating the copy changed the original")

  def test_the_original_has_its_own_buffer(self):
    img = Image.blank(2, 2, (1, 2, 3, 4))
    clone = img.copy()
    img.data[0] = 200
    self.assertEqual(clone.data[0], 1, "mutating the original changed the copy")

  def test_copy_is_writable(self):
    self.assertIsInstance(Image.blank(1, 1).copy().data, bytearray)


class TestRepr(unittest.TestCase):
  def test_repr_shows_the_dimensions(self):
    self.assertEqual(repr(Image.blank(640, 480)), "Image(640x480)")


if __name__ == "__main__":
  unittest.main()
