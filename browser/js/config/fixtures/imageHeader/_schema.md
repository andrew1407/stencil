# Image-header sniffer corpus

Pins the codec-free header read that sizes an image without decoding it: mcp
(`src/imagesize.rs`), bot (`Application/Llm/ImageDimensionReader.cs`), pystencil
(`codecs/sniff.py`, `image_dimensions`), cli (`src/scrape/sniff.zig`) and desktop
(`src/io/mediaTypes.cpp`, `sniffImageHeader`).

`cases.json` — `{ name, base64, expect }`:

- `base64` — the leading bytes of a file, as a sniffer is handed them.
- `expect` — `{ format, width, height }`, or `null` when the bytes must not be measured.
  `format` is one of `png`, `gif`, `bmp`, `jpeg`, `webp`; a surface that reports no format
  compares the size alone.

The rules the corpus holds every sniffer to:

- A zero side is no answer, and a PNG side past 2³¹−1 breaks the PNG spec: both are `null`.
- BMP stores a top-down image as a negative height; the size is its magnitude.
- A JPEG is walked segment by segment to its first frame header (SOF0–SOF15 except DHT `C4`,
  JPG `C8` and DAC `CC`). Fill bytes and standalone markers (`01`, `D0`–`D9`) carry no length;
  bytes that are no marker where one should start are skipped, as the decoder skips them; a
  segment length under 2 ends the walk with `null`.
- WebP needs its chunk's own signature: the `9D 01 2A` start code for lossy `VP8 `, the `2F`
  byte for lossless `VP8L`. Lossy sizes drop their two scale bits.

A surface that deliberately does not read a format pins its measured result in its own
override list, with a one-line note; the corpus never changes to fit a surface.
