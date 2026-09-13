from __future__ import annotations

"""One-shot mode: the argparse surface and the two non-interactive pipelines
(edit-and-write, and source-site scrape).
"""

import argparse
import re
import urllib.parse
from typing import TextIO

from .. import codecs
from .._severity import emit_error
from ..editor import Editor
from ..sitesource import download_media, scan_page
from .blank import BlankSpec

def _resolve_output(out: str) -> tuple[str, str]:
  """Return (path, fmt): fill in a ``.png`` extension when one is missing/unknown."""
  fmt = codecs.format_from_ext(out)
  if fmt is None: return (out + ".png", "png")
  return (out, fmt)


def _build_parser() -> argparse.ArgumentParser:
  """Construct the argparse parser for the one-shot + console modes."""
  p = argparse.ArgumentParser(
    prog="stencil-py",
    description="Headless image editing over the shared Stencil core "
    "(Python port of the Zig CLI).",
  )
  p.add_argument("-i", "--input", help="image source: a file path or http(s):// URL")
  # --blank takes 0..3 trailing tokens ([format] [w h] [color]); we disentangle them
  # (and any greedily-swallowed output path) in _consume_blank.
  p.add_argument(
    "--blank",
    nargs="*",
    metavar="TOKEN",
    help="create a blank page: [format] [w h] [color] (default A4 @ 96dpi, white)",
  )
  p.add_argument("-f", "--frame", type=int, default=0, help="video frame index (parity only)")
  p.add_argument("-c", "--crop", help='crop spec, e.g. "x1=10%% x2=90%% y1=10%% y2=90%%"')
  p.add_argument("--album", action="store_true", help="derive the missing crop axis from the page")
  p.add_argument("-r", "--rotate", type=int, default=0, help="rotate by N quarter-turns (N×90°)")
  p.add_argument("-l", "--layout", help="layout JSON (path or URL) to DRAW onto the image")
  p.add_argument(
    "--filter",
    dest="filter",
    help="bw | sepia | invert | contour | none | a colour name/#hex (duotone)",
  )
  p.add_argument("--save-layout", dest="save_layout", help="export the structured layout JSON here")
  # ── .stc script modes (mutually exclusive with each other) ──
  p.add_argument(
    "--script",
    metavar="FILE",
    help="run a .stc script ('-' reads stdin); with no @source block it edits --input",
  )
  p.add_argument(
    "--script-check", dest="script_check", metavar="FILE",
    help="print the script's diagnostics and exit 1 on an error",
  )
  p.add_argument(
    "--script-plan", dest="script_plan", metavar="FILE",
    help="print the script lowered to an op plan (JSON, stdout)",
  )
  p.add_argument(
    "--confine-output", dest="confine_output", action="store_true",
    help="refuse a @save that lands outside the working directory",
  )
  # ── source-site scrape mode (mutually exclusive with --input/--blank) ──
  p.add_argument(
    "--source-site",
    dest="source_site",
    metavar="URL",
    help="scrape a web page's media into the output DIRECTORY (activates scrape mode)",
  )
  p.add_argument(
    "--source-count", dest="source_count", type=int, default=None,
    help="items per page/group (default 5; 0 = all)",
  )
  p.add_argument("--group", dest="group", type=int, default=0, help="0-based page index")
  p.add_argument(
    "--source-filter", dest="source_filter", default="all",
    help="category tokens |-joined: img|video|background|poster (all = every category)",
  )
  p.add_argument(
    "--source-format", dest="source_format", default="all",
    help="format tokens |-joined, e.g. png|jpg|webp|mp4 (all = every format)",
  )
  p.add_argument(
    "--source-name", dest="source_name", default=None,
    help="regex (case-insensitive) matched against each media URL",
  )
  p.add_argument("--source-min-width", dest="source_min_width", type=int, default=0,
         help="inclusive min width in px (0 = unset)")
  p.add_argument("--source-max-width", dest="source_max_width", type=int, default=0,
         help="inclusive max width in px (0 = unset)")
  p.add_argument("--source-min-height", dest="source_min_height", type=int, default=0,
         help="inclusive min height in px (0 = unset)")
  p.add_argument("--source-max-height", dest="source_max_height", type=int, default=0,
         help="inclusive max height in px (0 = unset)")
  p.add_argument(
    "--console",
    "--repl",
    dest="console",
    action="store_true",
    help="start the interactive /command REPL instead of a one-shot run",
  )
  p.add_argument("output", nargs="?", help="result image path (extension auto-filled to .png)")
  return p


def _run_pipeline(args: argparse.Namespace, err: TextIO) -> int:
  """Execute the one-shot pipeline; returns a process exit code."""
  editor = Editor()

  # 1) Source — --blank and --input are mutually exclusive (mirror the Zig parser).
  blank_leftover: list[str] = list()
  if args.blank is not None and args.input is not None:
    emit_error(err, "--input and --blank are mutually exclusive")
    return 2
  if args.blank is not None:
    spec = BlankSpec.parse(args.blank)
    blank_leftover = spec.leftover
    editor.blank(spec.width, spec.height, spec.color, page=spec.page or "A4")
  elif args.input is not None:
    editor.load(args.input, frame=args.frame)
  else:
    emit_error(
      err, "no source — pass --input <path|url> or --blank [format] [w h] [color]"
    )
    return 2

  # An output path argparse swallowed into --blank's token list takes precedence
  # over a separately-parsed positional (there can only be one in practice).
  output = blank_leftover[-1] if blank_leftover else args.output

  # 2) crop → 3) rotate.
  if args.crop: editor.crop(args.crop, album=args.album)
  if args.rotate: editor.rotate(args.rotate)

  # 4) filter (explicit --filter; raises on an unrecognized value). It belongs to the
  #    picture — Editor.result() always renders the lines over it, whatever the order.
  if args.filter: editor.apply_filter(args.filter)

  # 5) draw the layout (append its lines; the editor reads a path/URL/inline JSON).
  if args.layout: editor.draw(args.layout)

  # 6) write the image and/or the layout. At least one output is required.
  if not output and not args.save_layout:
    emit_error(err, "no output — give a result image path and/or --save-layout")
    return 2

  if output:
    path, fmt = _resolve_output(output)
    img = editor.save(path, fmt)
    err.write("wrote %s (%dx%d)\n" % (path, img.width, img.height))
  if args.save_layout is not None:
    lay_path = editor.save_layout(args.save_layout)
    w, h = editor.image_size
    err.write("wrote %s (%dx%d)\n" % (lay_path, w, h))
  return 0


def __unset0(v: int) -> int:
  """Map a CLI dimension flag (``0`` = unset) to the scan_page convention (``-1``)."""
  return v if v and v > 0 else -1


def _run_scrape(args: argparse.Namespace, err: TextIO) -> int:
  """Execute source-site scrape mode: fetch/filter/download; print the §3 stderr lines."""
  # Scrape mode is mutually exclusive with the editing sources (mirror DuplicateSource).
  if args.input is not None or args.blank is not None:
    emit_error(err, "--source-site cannot be combined with --input or --blank")
    return 2
  url = args.source_site
  out_dir = args.output or "."
  host = urllib.parse.urlparse(url).hostname or ""
  # Apply the user-facing --source-count default here at the entry layer (parity with the
  # Zig CLI's scrape.effectiveCount): absent = 5, 0 = all (None), N = N. scan_page's own
  # count=None primitive still means "all".
  sc = args.source_count
  count = 5 if sc is None else (None if sc == 0 else sc)
  # Announce the scrape before the page fetch + downloads (which can take a while) rather than
  # sitting silent until the first `wrote`. Mirrors the Zig CLI's scrape.run leading line; it
  # carries none of the parsed prefixes, so the mcp/bot adapters ignore it.
  err.write("scraping %s…\n" % url)
  try:
    items = scan_page(
      url,
      category=args.source_filter or "all",
      formats=args.source_format or "all",
      name=args.source_name,
      min_width=__unset0(args.source_min_width),
      max_width=__unset0(args.source_max_width),
      min_height=__unset0(args.source_min_height),
      max_height=__unset0(args.source_max_height),
      count=count,
      group=args.group or 0,
    )
  except re.error:
    # Mirror the Zig CLI's fail-fast on a bad --source-name pattern.
    emit_error(err, "invalid --source-name regex '%s'" % args.source_name)
    return 1
  paths = download_media(items, out_dir, host=host, err=err)
  if not paths:
    emit_error(err, "no media matched at %s" % url)
    return 1
  err.write("scraped %d file(s) from %s into %s\n" % (len(paths), host, out_dir))
  return 0
