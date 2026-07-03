from __future__ import annotations

"""Command-line front-end for pystencil — ``python -m pystencil`` / ``stencil-py``.

This is the Python counterpart of the Zig CLI (``cli/``). It offers the same two
modes over the shared core, driving an :class:`~pystencil.editor.Editor`:

* **One-shot pipeline** (default): flags mirror the Zig CLI as far as is
  practical — ``-i/--input``, ``--blank``, ``-f/--frame``, ``-c/--crop``,
  ``--album``, ``-r/--rotate``, ``-l/--layout`` (draw), ``--filter``,
  ``--save-layout`` (export the structured layout), and a positional output
  image. Steps run in the fixed order **source → crop → rotate → draw-layout →
  filter**, then the image and/or layout are written. On success the canonical
  ``wrote {path} ({w}x{h})`` line is printed to stderr (matching the Zig
  contract); failures print ``error: …`` and return a non-zero code.

* **Console / REPL** (``--console`` / ``--repl``): reads ``/command <args>``
  lines mirroring the Zig console grammar (``cli/src/console/commands.zig``):
  ``/upload`` (``/open``/``/load``), ``/blank`` (``/new``), ``/format``,
  ``/crop``, ``/rotate`` (``/rot``), ``/filter`` (+ ``/bw`` ``/sepia``
  ``/none``), ``/apply`` (``/draw``), ``/layout`` (export), ``/save``,
  ``/undo`` ``/redo`` ``/reset``, ``/status``, ``/connect``, ``/projects``,
  ``/fetch``, ``/help``, ``/exit``. Messages go to stderr, the CLI's human
  channel, exactly like the Zig REPL.

Both modes reuse the exact same ``core/`` transforms as the browser and Zig
front-ends, so results are identical by construction.
"""

import argparse
import sys
from typing import List, Optional, Sequence, TextIO, Tuple

from . import codecs
from .editor import Editor
from .server import ConnectionManager, ServerError, normalize_url


# Image extensions Python can actually encode (codecs is PNG/BMP only). A bare or
# unknown output extension falls back to PNG, matching the codecs default.
def _resolve_output(out: str) -> Tuple[str, str]:
    """Return (path, fmt): fill in a ``.png`` extension when one is missing/unknown."""
    fmt = codecs.format_from_ext(out)
    if fmt is None:
        return (out + ".png", "png")
    return (out, fmt)


def _is_int(tok: str) -> bool:
    """True when ``tok`` is a base-10 integer (used to spot --blank dimensions)."""
    try:
        int(tok)
        return True
    except ValueError:
        return False


def _consume_blank(
    tokens: Sequence[str],
) -> Tuple[Optional[str], Optional[int], Optional[int], str, List[str]]:
    """Parse ``[format] [w h] [color]`` from a --blank token list (port of args.parseBlank).

    Returns ``(page, width, height, color, leftover)``. An optional leading page-format
    token names the page (case-insensitive, e.g. "b5"); it is mutually exclusive with an
    explicit ``w h`` pair. A leading integer requires a matching height (else it is
    malformed). A colour is consumed only when the core recognizes it. Any remaining
    tokens are returned as ``leftover`` so the caller can recover an output path that
    argparse greedily swallowed.
    """
    from .core import get_core

    core = get_core()
    page: Optional[str] = None
    width: Optional[int] = None
    height: Optional[int] = None
    color = "white"
    i = 0
    toks = list(tokens)
    if toks and not _is_int(toks[0]):
        page = core.canonical_page_format(toks[0])
        if page is not None:
            i = 1
    if i < len(toks) and _is_int(toks[i]):
        # A format token and explicit dimensions are mutually exclusive (pinned).
        if page is not None:
            raise ValueError("--blank takes a page format or explicit w h, not both")
        # A width is only meaningful paired with a height.
        if i + 1 >= len(toks) or not _is_int(toks[i + 1]):
            raise ValueError("--blank width needs a matching height")
        width = int(toks[i])
        height = int(toks[i + 1])
        i += 2
    if i < len(toks):
        tok = toks[i]
        # Only swallow the colour when it is one (and not a flag), like the Zig parser.
        if not tok.startswith("-") and core.parse_color(tok) is not None:
            color = tok
            i += 1
    leftover = toks[i:]
    return (page, width, height, color, leftover)


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
    blank_leftover: List[str] = []
    if args.blank is not None and args.input is not None:
        err.write("error: --input and --blank are mutually exclusive\n")
        return 2
    if args.blank is not None:
        page, width, height, color, blank_leftover = _consume_blank(args.blank)
        editor.blank(width, height, color, page=page or "A4")
    elif args.input is not None:
        editor.load(args.input, frame=args.frame)
    else:
        err.write(
            "error: no source — pass --input <path|url> or --blank [format] [w h] [color]\n"
        )
        return 2

    # An output path argparse swallowed into --blank's token list takes precedence
    # over a separately-parsed positional (there can only be one in practice).
    output = blank_leftover[-1] if blank_leftover else args.output

    # 2) crop → 3) rotate.
    if args.crop:
        editor.crop(args.crop, album=args.album)
    if args.rotate:
        editor.rotate(args.rotate)

    # 4) draw the layout (append its lines; the editor reads a path/URL/inline JSON).
    if args.layout:
        editor.draw(args.layout)

    # 5) filter (explicit --filter; raises on an unrecognized value).
    if args.filter:
        editor.apply_filter(args.filter)

    # 6) write the image and/or the layout. At least one output is required.
    if not output and not args.save_layout:
        err.write("error: no output — give a result image path and/or --save-layout\n")
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


# ── interactive console (REPL) ─────────────────────────────────────────────────
def _parse_command(line: str) -> Tuple[str, str]:
    """Split a line into (verb, arg) at the first whitespace, dropping one leading '/'.

    Port of commands.zig parseCommand: ``/upload x`` ≡ ``upload x``; a ``://`` in the
    argument is preserved.
    """
    s = line.strip()
    if s.startswith("/"):
        s = s[1:].lstrip(" \t")
    if not s:
        return ("", "")
    parts = s.split(None, 1)
    if len(parts) == 1:
        return (parts[0], "")
    return (parts[0], parts[1].strip())


# Console help text, mirroring the Zig REPL's command listing.
_HELP = """commands:
  /upload <path|url>     load an image (aliases: open, load)
  /blank [f] [w h] [color]  create a blank page, f = a page format (alias: new)
  /format [name|custom w h]  list the page formats / set the session's format
  /crop <spec> [album]   crop, e.g. x1=10% x2=90% y1=10% y2=90%
  /rotate <int>          rotate int×90° (aliases: rot, turn)
  /filter <mode>         bw | sepia | invert | contour | none | colour (also: /bw /sepia /none /tint)
  /apply <path|url>      draw a layout JSON onto the image (alias: draw)
  /layout [path]         EXPORT the structured layout JSON
  /save [path]           write the working image to a file
  /undo /redo /reset     walk the edit history
  /status                show the working image (alias: info)
  /connect <url[ url2]>  connect to collaboration server(s)
  /projects [url]        list a server's projects (alias: ls)
  /fetch <name> [url]    load a server project's image (alias: pull)
  /help                  this list (aliases: ?, h)
  /exit                  leave (aliases: quit, q)"""


class _Repl:
    """The interactive console state: one Editor plus the server connections."""

    def __init__(self, out: TextIO) -> None:
        self._editor = Editor()
        self._manager = ConnectionManager()
        self._out = out

    def _say(self, msg: str) -> None:
        """Emit a human-readable line to the console channel (stderr)."""
        self._out.write(msg + "\n")

    def run(self, src: TextIO) -> int:
        """Read/dispatch ``/command`` lines until EOF or ``/exit``."""
        for raw in src:
            word, arg = _parse_command(raw)
            if not word:
                continue
            try:
                if self._dispatch(word, arg):
                    break
            except (ValueError, RuntimeError, OSError, ServerError) as e:
                self._say("error: %s" % e)
        return 0

    def _dispatch(self, word: str, arg: str) -> bool:
        """Run one command. Returns True to request exiting the REPL."""
        w = word.lower()
        if w in ("exit", "quit", "q"):
            return True
        if w in ("help", "?", "h"):
            self._say(_HELP)
        elif w in ("upload", "open", "load"):
            self._cmd_upload(arg)
        elif w in ("blank", "new"):
            self._cmd_blank(arg)
        elif w == "format":
            self._cmd_format(arg)
        elif w == "crop":
            self._cmd_crop(arg)
        elif w in ("rotate", "rot", "turn"):
            self._editor.rotate(int(arg))
            self._say_status_brief("rotated")
        elif w == "filter":
            if not arg:
                # Bare /filter lists the possible variants instead of erroring.
                self._cmd_filter_variants()
            else:
                self._editor.apply_filter(arg)
                self._say_status_brief("filtered")
        elif w in ("bw", "sepia", "none"):
            self._editor.apply_filter(w)
            self._say_status_brief("filtered")
        elif w in ("tint", "color", "colour"):
            self._editor.apply_filter(arg)
            self._say_status_brief("filtered")
        elif w in ("apply", "draw"):
            self._editor.draw(arg)
            self._say_status_brief("drew layout")
        elif w in ("layout", "savelayout", "exportlayout"):
            path = self._editor.save_layout(arg or None)
            self._say("exported layout -> %s" % path)
        elif w in ("save", "write"):
            self._cmd_save(arg)
        elif w in ("undo", "u"):
            self._say("undid" if self._editor.undo() else "nothing to undo")
        elif w in ("redo", "r"):
            self._say("redid" if self._editor.redo() else "nothing to redo")
        elif w in ("reset", "revert"):
            self._editor.reset()
            self._say("reset to original")
        elif w in ("status", "info", "image"):
            self._cmd_status()
        elif w == "connect":
            self._cmd_connect(arg)
        elif w in ("connections", "servers"):
            self._cmd_connections()
        elif w in ("projects", "ls"):
            self._cmd_projects(arg)
        elif w in ("fetch", "pull"):
            self._cmd_fetch(arg)
        elif w in ("drop", "close", "forget"):
            self._editor = Editor()
            self._say("dropped the working image")
        else:
            self._say("error: unknown command '/%s' (try /help)" % word)
        return False

    # ── command implementations ──
    def _cmd_upload(self, arg: str) -> None:
        if not arg:
            self._say("error: /upload needs a path or URL")
            return
        self._editor.load(arg)
        w, h = self._editor.image_size
        self._say('loaded "%s" (%dx%d)' % (self._editor.name, w, h))

    def _cmd_blank(self, arg: str) -> None:
        from .core import get_core

        core = get_core()
        tokens = arg.split() if arg else []
        page, width, height, color, _ = _consume_blank(tokens)
        # Capture the session's /format pick up front (port of the Zig console's doBlank,
        # which captures it before loadImage → clearAll → clearFormat wipes it).
        prev_page = core.canonical_page_format(self._editor.page_format)
        prev_custom = self._editor.page_format.lower() == "custom"
        prev_w = self._editor.custom_page_width
        prev_h = self._editor.custom_page_height
        custom_w = custom_h = 0.0
        if page is None and width is None:
            # No explicit format/dims: the session's /format choice drives the default
            # page (mirroring the Zig console, where page_size drives /blank).
            if prev_custom and prev_w > 0 and prev_h > 0:
                custom_w, custom_h = prev_w, prev_h
                width, height = core.default_blank_size_px(custom_w, custom_h)
            else:
                # An unknown adopted name — or "custom" without both cm dims — maps to
                # None → the default A4 blank, exactly like the console's fall-through
                # (canonicalPageFormat -> null) in doBlank.
                page = prev_page
        self._editor.blank(width, height, color, page=page or "A4")
        # Keep the page the blank was actually created on as the session's picked
        # format, so it drives the next bare /blank and the exported layout's pageSize
        # (mirror of the Zig console's doBlank -> session.setPageSize). Explicit dims
        # size the blank but keep the previous /format pick, matching the console and
        # the Telegram bot; only an unusable pick (unknown name / dimension-less
        # custom) ends up cleared.
        if page is not None:
            self._editor.set_page_format(page)
        elif custom_w > 0 and custom_h > 0:
            self._editor.set_page_format("custom", custom_w, custom_h)
        elif prev_page is not None:
            self._editor.set_page_format(prev_page)
        elif prev_custom and prev_w > 0 and prev_h > 0:
            self._editor.set_page_format("custom", prev_w, prev_h)
        else:
            self._editor.set_page_format("")
        w, h = self._editor.image_size
        self._say("blank %dx%d (%s)" % (w, h, color))

    def _cmd_format(self, arg: str) -> None:
        """/format: bare lists the formats; a name sets; ``custom <w> <h>`` sets custom."""
        from .core import get_core

        core = get_core()
        parts = arg.split()
        if not parts:
            # List every named format with its portrait cm size, marking the current one.
            current = self._editor.page_format
            for name in core.page_formats():
                wcm, hcm = core.named_page_size(name) or (0.0, 0.0)
                marker = "*" if name == current else " "
                self._say("%s %-4s %g×%gcm" % (marker, name, wcm, hcm))
            self._say("%s custom <w> <h>  a custom page in cm"
                      % ("*" if current == "custom" else " "))
            return
        if parts[0].lower() == "custom":
            # One error path for unparsable, NaN/inf and out-of-range dims, mirroring
            # the Zig console's parseCmDim (0.1–500 cm; float() accepts "nan"/"inf",
            # so set_page_format's range check must also gate the REPL input).
            try:
                wcm, hcm = float(parts[1]), float(parts[2])
                self._editor.set_page_format("custom", wcm, hcm)
            except (IndexError, ValueError):
                self._say(
                    "error: custom takes width + height in cm (0.1-500) — e.g. '/format custom 21 29.7'"
                )
                return
            self._say("page format custom (%g×%gcm)" % (wcm, hcm))
            return
        try:
            self._editor.set_page_format(parts[0])
        except ValueError:
            self._say("error: unknown page format '%s' — type '/format' to list formats"
                      % parts[0])
            return
        name = self._editor.page_format
        wcm, hcm = core.named_page_size(name) or (0.0, 0.0)
        self._say("page format %s (%g×%gcm)" % (name, wcm, hcm))

    def _cmd_filter_variants(self) -> None:
        """Bare /filter: list the possible modes (a bare required-arg command never errors)."""
        self._say("filters:")
        self._say("  bw        black & white")
        self._say("  sepia     warm sepia tone")
        self._say("  invert    negative colours")
        self._say("  contour   edge outline (dark lines on white)")
        self._say("  none      remove the filter")
        self._say("  <colour>  a colour name/#hex duotone tint")

    def _cmd_crop(self, arg: str) -> None:
        # Pull a standalone "album"/"--album" token out of the spec (port of stripAlbum).
        album = False
        kept: List[str] = []
        for tok in arg.split():
            if tok.lower() in ("album", "--album"):
                album = True
            else:
                kept.append(tok)
        self._editor.crop(" ".join(kept), album=album)
        self._say_status_brief("cropped")

    def _cmd_save(self, arg: str) -> None:
        if not arg:
            self._say("error: /save needs a path here (server push is not supported in the Python REPL)")
            return
        path, fmt = _resolve_output(arg)
        img = self._editor.save(path, fmt)
        self._say("wrote %s (%dx%d)" % (path, img.width, img.height))

    def _say_status_brief(self, verb: str) -> None:
        if not self._editor.has_image():
            self._say("error: no image loaded")
            return
        w, h = self._editor.image_size
        self._say("%s -> %dx%d" % (verb, w, h))

    def _cmd_status(self) -> None:
        if not self._editor.has_image():
            self._say("no image loaded")
            return
        w, h = self._editor.image_size
        self._say('image "%s" %dx%d' % (self._editor.name, w, h))

    def _cmd_connect(self, arg: str) -> None:
        urls = arg.split()
        if not urls:
            self._say("error: /connect needs one or more server URLs")
            return
        for url in urls:
            try:
                self._manager.connect(url)
                self._say("connected %s" % normalize_url(url))
            except (ServerError, OSError, ValueError) as e:
                self._say("error: could not connect to %s (%s)" % (url, e))

    def _cmd_connections(self) -> None:
        conns = self._manager.connections
        if not conns:
            self._say("no servers connected")
            return
        for url in conns:
            self._say(url)

    def _cmd_projects(self, arg: str) -> None:
        url = arg.strip()
        if url:
            conn = self._manager.get(url)
            if conn is None:
                self._say("error: not connected to %s" % url)
                return
            projects = conn.list_projects()
        else:
            projects = self._manager.remote_projects()
        if not projects:
            self._say("no projects")
            return
        for proj in projects:
            name = proj.get("name", "?") if isinstance(proj, dict) else str(proj)
            pid = proj.get("id", "?") if isinstance(proj, dict) else ""
            self._say("%-24s %s" % (name, pid))

    def _cmd_fetch(self, arg: str) -> None:
        parts = arg.split()
        if not parts:
            self._say("error: /fetch needs a project name")
            return
        name = parts[0]
        url = parts[1] if len(parts) > 1 else None
        conns = [self._manager.get(url)] if url else None
        if conns is None:
            conns = [self._manager.get(u) for u in self._manager.connections]
        for conn in conns:
            if conn is None:
                continue
            for proj in conn.list_projects():
                if isinstance(proj, dict) and proj.get("name") == name:
                    data = conn.get_file(proj["id"], "original")
                    self._editor.load(bytes(data), name=name)
                    w, h = self._editor.image_size
                    self._say('fetched "%s" (%dx%d)' % (name, w, h))
                    return
        self._say('error: no server project named "%s"' % name)


def main(argv: Optional[Sequence[str]] = None) -> int:
    """Entry point for ``python -m pystencil`` and the ``stencil-py`` script.

    ``argv`` defaults to ``sys.argv[1:]``. Returns a process exit code (0 on
    success, non-zero on error). All human-facing output goes to **stderr** so
    that any piped/automation use stays clean.
    """
    parser = _build_parser()
    args = parser.parse_args(list(argv) if argv is not None else None)
    err = sys.stderr

    if args.console:
        return _Repl(err).run(sys.stdin)

    try:
        return _run_pipeline(args, err)
    except ServerError as e:
        err.write("error: %s\n" % e)
        return 1
    except (ValueError, RuntimeError, OSError, codecs.CodecError) as e:
        err.write("error: %s\n" % e)
        return 1


if __name__ == "__main__":
    sys.exit(main())
