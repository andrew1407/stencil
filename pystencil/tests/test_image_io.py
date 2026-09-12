from __future__ import annotations

"""``Image`` round-trips: the pure-Python codecs and the disk save/open path.

Encoding and decoding are format-symmetric, so every case asserts on the pixels
that come back rather than on the bytes in between; ``save``/``open`` add the
extension-to-format inference on top.
"""

import os
import tempfile
import unittest

from pystencil import codecs
from pystencil.image import Image


class TestEncodeDecode(unittest.TestCase):
  """Round-tripping through the pure-Python codecs."""

  def setUp(self):
    # A small non-uniform image: a uniform one would hide channel-order bugs.
    self.img = Image(
      2,
      2,
      bytearray(
        [
          255, 0, 0, 255,
          0, 255, 0, 255,
          0, 0, 255, 128,
          9, 9, 9, 0,
        ]
      ),
    )

  def test_png_round_trip_preserves_every_pixel(self):
    back = Image.decode(self.img.encode("png"))
    self.assertEqual((back.width, back.height), (2, 2))
    self.assertEqual(bytes(back.data), bytes(self.img.data))

  def test_bmp_round_trip_preserves_every_pixel(self):
    back = Image.decode(self.img.encode("bmp"))
    self.assertEqual((back.width, back.height), (2, 2))
    self.assertEqual(bytes(back.data), bytes(self.img.data))

  def test_encode_defaults_to_png(self):
    self.assertEqual(codecs.sniff(self.img.encode()), "png")

  def test_encode_format_is_case_insensitive(self):
    self.assertEqual(codecs.sniff(self.img.encode("PNG")), "png")
    self.assertEqual(codecs.sniff(self.img.encode("BmP")), "bmp")

  def test_unsupported_encode_format_raises(self):
    with self.assertRaises(codecs.CodecError) as ctx:
      self.img.encode("jpeg")
    self.assertIn("jpeg", str(ctx.exception))

  def test_decode_returns_a_writable_buffer(self):
    """Decoded images go straight into in-place core ops."""
    back = Image.decode(self.img.encode("png"))
    self.assertIsInstance(back.data, bytearray)


class TestOpenSave(unittest.TestCase):
  """Disk I/O, including the extension-to-format inference."""

  def setUp(self):
    self.dir = tempfile.TemporaryDirectory()
    self.addCleanup(self.dir.cleanup)
    self.img = Image.blank(3, 2, (12, 34, 56, 255))

  def path(self, name):
    return os.path.join(self.dir.name, name)

  def test_save_then_open_round_trips(self):
    p = self.path("out.png")
    self.img.save(p)
    back = Image.open(p)
    self.assertEqual((back.width, back.height), (3, 2))
    self.assertEqual(bytes(back.data), bytes(self.img.data))

  def test_format_is_inferred_from_the_extension(self):
    p = self.path("out.bmp")
    self.img.save(p)
    with open(p, "rb") as fh:
      self.assertEqual(codecs.sniff(fh.read()), "bmp")

  def test_an_explicit_format_overrides_the_extension(self):
    """A .png name written as BMP stays BMP — the argument wins."""
    p = self.path("misnamed.png")
    self.img.save(p, fmt="bmp")
    with open(p, "rb") as fh:
      self.assertEqual(codecs.sniff(fh.read()), "bmp")

  def test_an_unknown_extension_defaults_to_png(self):
    p = self.path("out.xyz")
    self.img.save(p)
    with open(p, "rb") as fh:
      self.assertEqual(codecs.sniff(fh.read()), "png")

  def test_a_missing_extension_defaults_to_png(self):
    p = self.path("no_extension")
    self.img.save(p)
    with open(p, "rb") as fh:
      self.assertEqual(codecs.sniff(fh.read()), "png")

  def test_save_overwrites_an_existing_file(self):
    p = self.path("out.png")
    self.img.save(p)
    Image.blank(1, 1, (0, 0, 0, 255)).save(p)
    self.assertEqual((Image.open(p).width, Image.open(p).height), (1, 1))

  def test_open_a_missing_file_raises_oserror(self):
    with self.assertRaises(OSError):
      Image.open(self.path("nope.png"))

  def test_open_a_non_image_raises_a_codec_error(self):
    p = self.path("junk.png")
    with open(p, "wb") as fh:
      fh.write(b"this is not an image")
    with self.assertRaises(codecs.CodecError):
      Image.open(p)
