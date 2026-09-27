"""JPEG through the CLI's stb units in the native library: the fixture's pinned pixels, the
header cap, clean errors, the quality-90 encoder, and the ``.jpg`` paths a save, a script
and a folder source take. Native cases skip with the reason when the build left JPEG out.
"""

from __future__ import annotations

import contextlib
import hashlib
import io
import json
import os
import struct
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from tests.helpers.nativecase import require_stb

from pystencil import cli, codecs, scriptpaths
from pystencil.editor import Editor
from pystencil.image import Image

# Baseline 4:2:0 with restart markers (macOS ImageIO at quality 75), so the upsampler runs.
FIXTURE = Path(__file__).resolve().parent / "gradient420.jpg"
FIXTURE_SIZE = (37, 23)
FIXTURE_PIXELS_SHA256 = "14aecc67e352395bc635664c8c136aad8e17459952655f5ab2c31c15f2226458"
# The fixture cut at 800 bytes, mid-scan: stb stops at the missing restart marker and the
# planes it never wrote come out of zeroed memory, never out of old heap.
CUT_SCAN_PIXELS_SHA256 = "83396e5aa314cd2577c926d046b9a1a39ae2945e6c3bb84ea2bee5dd72fc2543"
# JFIF, the q90 tables, the 4:4:4 frame and the Huffman tables: integers, so every platform's.
SMOOTH_HEADER_SHA256 = "2c8a3a8c1aae4aef92edcf7e7b1d59ab4f550217bf98c3f5116090391c85904a"
SMOOTH_SIZE = (40, 24)


def smooth() -> bytearray:
  """A 40x24 two-axis gradient: no hard edge, so a JPEG round trip stays close."""
  w, h = SMOOTH_SIZE
  return bytearray(b"".join(bytes((x * 6, y * 10, 128, 255)) for y in range(h) for x in range(w)))


def jpeg_header(data: bytes) -> bytes:
  """Every byte through the SOS segment — what the encoder derives from its quality."""
  at = data.index(b"\xff\xda")
  return data[: at + 2 + struct.unpack(">H", data[at + 2 : at + 4])[0]]


def pixel_sha(pixels) -> str:
  return hashlib.sha256(bytes(pixels)).hexdigest()


class JpegPathTests(unittest.TestCase):
  """Where a ``.jpg`` name now leads; no native library needed."""

  def test_both_spellings_name_the_jpeg_codec(self):
    self.assertEqual(codecs.format_from_ext("x.JPG"), "jpeg")
    self.assertEqual(codecs.format_from_ext("dir.png/y.jpeg"), "jpeg")

  def test_a_jpeg_source_saves_as_jpg_whatever_its_spelling(self):
    for ext in ("jpg", "JPEG", "jpeg"):
      self.assertEqual(scriptpaths.save_format(ext), "jpg")

  def test_a_folder_source_picks_up_jpegs(self):
    self.assertTrue(scriptpaths.is_media("a.jpg"))
    self.assertTrue(scriptpaths.is_media("b.JPEG"))

  def test_a_jpg_target_is_taken_verbatim(self):
    self.assertEqual(scriptpaths.resolve_target("out/exact.jpg", "a.png", None, "png"),
                     "out/exact.jpg")


class JpegHeaderCapTests(unittest.TestCase):
  """The ``MAX_SIDE`` refusal reads the header alone, before stb is ever reached."""

  def test_a_side_past_the_cap_is_refused_before_stb(self):
    data = bytearray(FIXTURE.read_bytes())
    at = data.index(b"\xff\xc0")
    data[at + 5 : at + 9] = struct.pack(">HH", 10, 20000)
    with mock.patch.object(codecs.jpeg, "_library", side_effect=AssertionError("reached stb")):
      with self.assertRaises(codecs.CodecError) as caught:
        codecs.decode(bytes(data))
    self.assertIn("20000x10, past the 16384-pixel side cap", str(caught.exception))


class JpegDecodeTests(unittest.TestCase):
  @classmethod
  def setUpClass(cls):
    require_stb()

  def test_the_fixture_decodes_to_its_pinned_pixels(self):
    w, h, pixels = codecs.decode(FIXTURE.read_bytes())
    self.assertEqual((w, h), FIXTURE_SIZE)
    self.assertEqual(pixel_sha(pixels), FIXTURE_PIXELS_SHA256)
    self.assertEqual(set(pixels[3::4]), {255})

  def test_a_bytearray_decodes_as_its_bytes_do(self):
    data = FIXTURE.read_bytes()
    self.assertEqual(codecs.decode(bytearray(data)), codecs.decode(data))

  def test_the_image_and_the_editor_open_a_jpeg(self):
    img = Image.open(str(FIXTURE))
    self.assertEqual((img.width, img.height), FIXTURE_SIZE)
    editor = Editor().load(str(FIXTURE))
    self.assertEqual(editor.image_size, FIXTURE_SIZE)
    self.assertEqual(editor.name, "gradient420")

  def test_a_file_cut_before_its_frame_errors_cleanly(self):
    data = FIXTURE.read_bytes()
    for cut in (20, 100):
      with self.subTest(cut=cut), self.assertRaises(codecs.CodecError) as caught:
        codecs.decode(data[:cut])
      self.assertIn("JPEG decode failed", str(caught.exception))

  def test_a_scan_cut_short_decodes_the_same_whatever_the_heap_held(self):
    data = FIXTURE.read_bytes()[:800]
    first = codecs.decode(data)
    churn = [bytes([i % 251]) * 4096 for i in range(64)]
    del churn
    self.assertEqual(codecs.decode(data), first)
    self.assertEqual(pixel_sha(first[2]), CUT_SCAN_PIXELS_SHA256)

  def test_corrupt_markers_error_cleanly(self):
    with self.assertRaises(codecs.CodecError) as caught:
      codecs.decode(b"\xff\xd8\xff" + bytes(range(256)) * 4)
    self.assertIn("JPEG decode failed", str(caught.exception))


class JpegEncodeTests(unittest.TestCase):
  @classmethod
  def setUpClass(cls):
    require_stb()

  def test_the_encoder_writes_the_clis_quality_90_tables(self):
    self.assertEqual(codecs.JPEG_QUALITY, 90)
    data = Image(*SMOOTH_SIZE, smooth()).encode("jpeg")
    self.assertEqual(hashlib.sha256(jpeg_header(data)).hexdigest(), SMOOTH_HEADER_SHA256)

  def test_a_round_trip_stays_close_and_drops_the_alpha(self):
    pixels = smooth()
    pixels[3::4] = bytes([7]) * (len(pixels) // 4)
    w, h, back = codecs.decode(codecs.encode_jpeg(*SMOOTH_SIZE, pixels))
    self.assertEqual((w, h), SMOOTH_SIZE)
    diffs = [abs(a - b) for i, (a, b) in enumerate(zip(back, pixels)) if i % 4 != 3]
    self.assertLessEqual(max(diffs), 12)
    self.assertLess(sum(diffs) / len(diffs), 2)
    self.assertEqual(set(back[3::4]), {255})

  def test_a_short_buffer_is_refused_before_the_call(self):
    with self.assertRaises(codecs.CodecError):
      codecs.encode_jpeg(*SMOOTH_SIZE, bytearray(16))

  def test_a_save_picks_jpeg_from_either_extension(self):
    with tempfile.TemporaryDirectory() as tmp:
      for name in ("a.jpg", "b.JPEG"):
        path = os.path.join(tmp, name)
        Image(*SMOOTH_SIZE, smooth()).save(path)
        with open(path, "rb") as handle:
          self.assertEqual(codecs.sniff(handle.read()), "jpeg")


class JpegScriptTests(unittest.TestCase):
  """A JPEG source through ``--script``: the folder picks it up and ``@save`` keeps JPEG."""

  def setUp(self):
    require_stb()
    tmp = tempfile.TemporaryDirectory()
    self.addCleanup(tmp.cleanup)
    old = os.getcwd()
    os.chdir(tmp.name)
    self.addCleanup(os.chdir, old)
    os.mkdir("shots")
    Image(*SMOOTH_SIZE, smooth()).save("shots/a.jpeg")

  @staticmethod
  def _run(argv):
    out, err = io.StringIO(), io.StringIO()
    with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
      code = cli.main(argv)
    return code, out.getvalue(), err.getvalue()

  def test_the_plan_and_the_run_name_the_same_jpg(self):
    with open("s.stc", "w", encoding="utf-8") as handle:
      handle.write("@source shots/:\n    @crop 25%\n    @save\n")
    _, out, _ = self._run(["--script-plan", "s.stc"])
    planned = json.loads(out)["blocks"][0]["saves"][0]["path"]
    code, _, err = self._run(["--script", "s.stc"])
    self.assertEqual(code, 0, err)
    self.assertEqual(planned, "shots/a-stencil.jpg")
    self.assertIn("wrote shots/a-stencil.jpg (20x12)", err)
    with open("shots/a-stencil.jpg", "rb") as handle:
      self.assertEqual(codecs.sniff(handle.read()), "jpeg")


if __name__ == "__main__":
  unittest.main()
