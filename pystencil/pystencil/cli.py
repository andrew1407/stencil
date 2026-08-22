from __future__ import annotations

"""Command-line front-end for pystencil — ``python -m pystencil`` / ``stencil-py``.

This is the Python counterpart of the Zig CLI (``cli/``). It offers the same two
modes over the shared core, driving an :class:`~pystencil.editor.Editor`:

* **One-shot pipeline** (default): flags mirror the Zig CLI as far as is
  practical — ``-i/--input``, ``--blank``, ``-f/--frame``, ``-c/--crop``,
  ``--album``, ``-r/--rotate``, ``-l/--layout`` (draw), ``--filter``,
  ``--save-layout`` (export the structured layout), and a positional output
  image. Steps run in the fixed order **source → crop → rotate → filter →
  draw-layout**, then the image and/or layout are written. On success the canonical
  ``wrote {path} ({w}x{h})`` line is printed to stderr (matching the Zig
  contract); failures print ``error: …`` and return a non-zero code.

* **Console / REPL** (``--console`` / ``--repl``): reads ``/command <args>``
  lines mirroring the Zig console grammar (``cli/src/console/commands.zig``):
  ``/upload`` (``/open``/``/load``), ``/blank`` (``/new``), ``/format``,
  ``/crop``, ``/rotate`` (``/rot``), ``/filter`` (+ ``/bw`` ``/sepia``
  ``/none``), ``/apply`` (``/draw``), ``/layout`` (export), ``/save``,
  ``/undo`` ``/redo`` ``/reset``, ``/status``, ``/connect``, ``/disconnect``,
  ``/delete`` (``/rm``), ``/projects``, ``/fetch``, ``/prompt`` (``/p``) +
  ``/llm`` + ``/chat`` (the LLM assistant — see ``llm-contract.md``),
  ``/help``, ``/exit``. Messages go to stderr, the
  CLI's human channel, exactly like the Zig REPL.

Both modes reuse the exact same ``core/`` transforms as the browser and Zig
front-ends, so results are identical by construction.
"""

import argparse
import json
import re
import sys
import urllib.parse
from typing import List, Optional, Sequence, TextIO, Tuple

from . import codecs
from ._severity import emit_error, error_line, note_line
from .editor import Editor
from .llm import (
    CONSOLE_SYSTEM_PROMPT,
    CONTINUATION_NOTE,
    EDGE_MAP_SUFFIX,
    MAX_UPLOAD_ATTACHMENTS,
    PROVIDERS,
    AskCard,
    Chat,
    ConsoleServer,
    LlmClient,
    LlmConfig,
    LlmError,
    ask_answer_text,
    blocked_open_url,
    console_context,
    execute_op_plan,
    format_ask,
    parse_op_plan,
    resolve_server,
    variant_slugs,
)
from .server import ConnectionManager, ServerError, normalize_url
from .sitesource import download_media, scan_page, _fetch


# §7 auto-continuation (amended): a plan whose actions CONTAIN a load op and drew NO
# layout continues once — crop/filter need no pixels, but the looking-work the model
# deferred does. On this surface the load ops are `blank` and the §10 console profile's
# `openUrl` — a `frame` op has no video input in the console.
def _load_only_plan(plan) -> bool:
    acts = getattr(plan, "actions", None) or []
    if not acts or getattr(plan, "variants", None):
        return False
    op_of = lambda a: a.get("op") if isinstance(a, dict) else getattr(a, "op", None)
    ops = [op_of(a) for a in acts]
    return ("blank" in ops or "openUrl" in ops) and "layout" not in ops


# Attachment cap for /prompt: the current image rides along for vision only when its
# encoded PNG stays under ~8 MiB (bigger payloads are skipped with a printed note).
_PROMPT_IMAGE_LIMIT = 8 * 1024 * 1024


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
    blank_leftover: List[str] = []
    if args.blank is not None and args.input is not None:
        emit_error(err, "--input and --blank are mutually exclusive")
        return 2
    if args.blank is not None:
        page, width, height, color, blank_leftover = _consume_blank(args.blank)
        editor.blank(width, height, color, page=page or "A4")
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
    if args.crop:
        editor.crop(args.crop, album=args.album)
    if args.rotate:
        editor.rotate(args.rotate)

    # 4) filter (explicit --filter; raises on an unrecognized value). It belongs to the
    #    picture — Editor.result() always renders the lines over it, whatever the order.
    if args.filter:
        editor.apply_filter(args.filter)

    # 5) draw the layout (append its lines; the editor reads a path/URL/inline JSON).
    if args.layout:
        editor.draw(args.layout)

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


def _unset0(v: int) -> int:
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
            min_width=_unset0(args.source_min_width),
            max_width=_unset0(args.source_max_width),
            min_height=_unset0(args.source_min_height),
            max_height=_unset0(args.source_max_height),
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


def _mask(secret: str) -> str:
    """Mask a credential for display: ``(none)`` when empty, else stars + last 4."""
    if not secret:
        return "(none)"
    return ("****" + secret[-4:]) if len(secret) > 4 else "****"


# Console help text, mirroring the Zig REPL's command listing.
_HELP = """commands:
  /upload <path|url>     load an image or a .stencil project (aliases: open, load)
  /source-upload <url> [index=0] [format=all] [name=] [minW=-1] [maxW=-1] [minH=-1] [maxH=-1]
                         scrape a page, load one filtered image (alias: scrape)
  /blank [f] [w h] [color]  create a blank page, f = a page format (alias: new)
  /format [name|custom w h]  list the page formats / set the session's format
  /crop <spec> [album]   crop, e.g. x1=10% x2=90% y1=10% y2=90%
  /rotate <int>          rotate int×90° (aliases: rot, turn)
  /filter <mode>         bw | sepia | invert | contour | none | colour (also: /bw /sepia /none /tint)
  /apply <path|url>      draw a layout JSON onto the image (alias: draw)
  /layout [path]         EXPORT the structured layout JSON
  /save [path]           write the working image (a .stencil path saves the project)
  /undo /redo /reset     walk the edit history
  /status                show the working image (alias: info)
  /delete <x.stencil>    delete a local .stencil project file (aliases: del, rm)
  /connect <url[ url2]> [token=<tok>]
                         connect to collaboration server(s); token= is required by a
                         server that has ADMIN_TOKEN set (it won't issue one)
  /disconnect [url]      close one connection (or the most recent when omitted)
  /projects [url]        list a server's projects (alias: ls)
  /fetch <name> [url]    load a server project's image (alias: pull)
  /prompt <text>         ask the LLM to edit the image (alias: p); plan actions apply to
                         the session, each variant lands as variant-<label>.png
  /llm [key value]       show the LLM config / set provider | url | model | key | server
  /chat [on|off|clear]   multi-turn /prompt + save chats with the project (default off)
  /help                  this list (aliases: ?, h)
  /exit                  leave (aliases: quit, q)"""


class _Repl:
    """The interactive console state: one Editor plus the server connections."""

    def __init__(self, out: TextIO) -> None:
        self._editor = Editor()
        # The last `ask` card the assistant printed (contract §11), so the next /prompt can
        # answer it by number. Replaced by a later card; cleared once answered.
        self._ask: Optional[AskCard] = None
        self._manager = ConnectionManager()
        self._out = out
        # In-session LLM provider config, seeded from the STENCIL_LLM_* env keys
        # (llm-contract.md §5); a bad env provider falls back to the defaults.
        try:
            self._llm = LlmConfig.from_env()
        except ValueError:
            self._llm = LlmConfig()
        # /prompt attachment memo: (editor, its revision, png bytes) — reused while the
        # edit state is unchanged so repeated prompts don't re-encode (contract §7).
        self._png_cache: Optional[Tuple[Editor, int, bytes]] = None
        # §2.1: this turn's /upload set — (media_type, png bytes, label) in upload
        # order, what a plan's `image` op indexes (the cli console's attachment
        # registry: capped at MAX_UPLOAD_ATTACHMENTS with the oldest falling off; a
        # /prompt marks the set used, so the next /upload starts a fresh one).
        self._attachments: List[Tuple[str, bytes, str]] = []
        self._attachments_used: bool = False
        # §12 chat persistence: /chat on|off (session-scoped, default OFF — /prompt
        # stays single-turn) and the multi-turn Chat used while it is on.
        self._chat_on: bool = False
        self._chat: Optional[Chat] = None
        # The active remote project — (ServerConnection, project id) recorded by
        # /fetch — so /chat clear can also drop the server-side `chat` file.
        # Cleared (with the conversation) whenever the working image is
        # replaced: see _image_replaced().
        self._remote: Optional[Tuple] = None
        # §10 clearChat: set by the plan_clear_chat hook during execution and
        # consumed by the end-of-turn confirm in _cmd_prompt.
        self._clear_chat_pending: bool = False
        # The command stream run() reads; the clearChat confirm reads its y/N
        # answer from the same stream (None until run() starts = declined).
        self._in: Optional[TextIO] = None

    def _say(self, msg: str) -> None:
        """Emit a human-readable line to the console channel (stderr)."""
        self._out.write(msg + "\n")

    def _err(self, msg: str) -> None:
        """Say `msg` as an ``error: `` line — the command did not do what was asked."""
        self._say(error_line(msg, self._out))

    def _note(self, msg: str) -> None:
        """Say `msg` as a ``note: `` line — it went ahead, with something worth saying."""
        self._say(note_line(msg, self._out))

    def _image_replaced(self, editor: Optional[Editor] = None) -> None:
        """The one chokepoint for "the working image was replaced".

        Installs ``editor`` when given (``/drop``'s fresh one; the in-place
        loaders pass nothing) and resets every piece of state scoped to the
        previous image: the active remote project identity and the running
        conversation. Without this, ``/chat clear`` after moving off a fetched
        project would delete the PREVIOUS project's server-side ``chat`` file.
        ``/fetch`` records the new ``_remote`` (and restores a saved chat)
        after coming through here — the same funnel shape as the Zig console
        Session's setRemote()/clearRemote() (cli/src/console/session.zig).
        """
        if editor is not None:
            self._editor = editor
        self._remote = None
        self._chat = None

    def run(self, src: TextIO) -> int:
        """Read/dispatch ``/command`` lines until EOF or ``/exit``."""
        self._in = src
        for raw in src:
            word, arg = _parse_command(raw)
            if not word:
                continue
            try:
                if self._dispatch(word, arg):
                    break
            except (ValueError, RuntimeError, OSError, ServerError) as e:
                self._err("%s" % e)
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
        elif w in ("source-upload", "sourceupload", "scrape"):
            self._cmd_source_upload(arg)
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
        elif w == "disconnect":
            self._cmd_disconnect(arg)
        elif w in ("delete", "del", "remove", "rm"):
            self._cmd_delete(arg)
        elif w in ("connections", "servers"):
            self._cmd_connections()
        elif w in ("projects", "ls"):
            self._cmd_projects(arg)
        elif w in ("fetch", "pull"):
            self._cmd_fetch(arg)
        elif w in ("prompt", "p"):
            self._cmd_prompt(arg)
        elif w == "llm":
            self._cmd_llm(arg)
        elif w == "chat":
            self._cmd_chat(arg)
        elif w in ("drop", "close", "forget"):
            self._image_replaced(Editor())
            self._say("dropped the working image")
        else:
            self._err("unknown command '/%s' (try /help)" % word)
        return False

    # ── command implementations ──
    def _cmd_upload(self, arg: str) -> None:
        if not arg:
            self._err("/upload needs a path or URL")
            return
        if arg.lower().endswith(".stencil"):
            # A whole .stencil project loads its image + layout + metadata at once
            # (Zig-console parity). A saved `chat` block restores the conversation
            # only while /chat is on (contract §12); otherwise it just stays on the
            # editor for a later re-save.
            self._editor.open_project(arg)
            self._image_replaced()
            if self._chat_on and self._editor.chat_doc:
                chat = Chat.from_doc(self._editor.chat_doc, self._llm_client())
                if chat.history:
                    self._chat = chat
        else:
            self._editor.load(arg)
            self._image_replaced()
            # §2.1: the uploads of one turn are its attachments — a later /prompt
            # sends them all and an `image` op indexes them. Registered as the wire's
            # PNG re-encode (the same normalization the cli console applies).
            self._add_attachment(arg)
        w, h = self._editor.image_size
        self._say('loaded "%s" (%dx%d)' % (self._editor.name, w, h))

    def _add_attachment(self, label: str) -> None:
        """Remember the just-loaded image as one of this turn's §2.1 attachments."""
        if self._attachments_used:
            self._attachments = []
            self._attachments_used = False
        try:
            data = self._editor.result(with_lines=False).encode("png")
        except Exception:  # noqa: BLE001 - best-effort, like the cli's att_bytes dupe
            return
        self._attachments.append(("image/png", data, label))
        # Past the cap the OLDEST upload falls off, keeping the newest §7-many.
        if len(self._attachments) > MAX_UPLOAD_ATTACHMENTS:
            del self._attachments[0]

    def _consume_attachments(self) -> None:
        """Mark the turn's attachments as spent (called once a /prompt used them):
        they stay indexable within the turn, and the next /upload starts a new set."""
        if self._attachments:
            self._attachments_used = True

    def _cmd_source_upload(self, arg: str) -> None:
        """/source-upload <url> [index= format= minW= maxW= minH= maxH=]: scrape a page,
        filter its image-category stills (img/bg/poster — NOT video), and load one."""
        parts = arg.split()
        if not parts:
            self._err("/source-upload needs a URL")
            return
        url = parts[0]
        opts = {"index": 0, "format": "all", "minw": -1, "maxw": -1, "minh": -1, "maxh": -1}
        custom_name: Optional[str] = None
        try:
            for tok in parts[1:]:
                if "=" not in tok:
                    continue
                key, val = tok.split("=", 1)
                key = key.strip().lower()
                if key == "format":
                    opts["format"] = val.strip() or "all"
                elif key == "name":
                    custom_name = val.strip() or None
                elif key in opts:
                    opts[key] = int(val)
        except ValueError:
            self._err("/source-upload options must be key=value (ints, format=/name=)")
            return
        # Announce the scrape before the fetch + download (parity with the Zig console's
        # doSourceUpload and the one-shot scrape's leading line) so the REPL isn't silent.
        self._say("scraping %s…" % url)
        # Image-category stills only; take ALL matches so the index selects over the full list.
        items = scan_page(
            url,
            category="img|background|poster",
            formats=opts["format"],
            min_width=opts["minw"],
            max_width=opts["maxw"],
            min_height=opts["minh"],
            max_height=opts["maxh"],
        )
        index = opts["index"]
        if not items:
            self._err("no image matched at %s" % url)
            return
        if index < 0 or index >= len(items):
            self._err("index %d out of range (0-%d)" % (index, len(items) - 1))
            return
        item = items[index]
        # Sub-resource URL harvested from the page: loopback blocked unless same-host.
        from .sitesource import _sub_strict

        page_host = urllib.parse.urlparse(url).hostname or ""
        data = _fetch(item.url, strict=_sub_strict(item.url, page_host))
        # Replace the working image via the session's load path (mirror /upload). A
        # name= token overrides the URL-derived project name.
        self._editor.load(
            bytes(data), name=custom_name or self._editor._name_from_url(item.url)
        )
        self._image_replaced()
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
        self._image_replaced()
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
                bullet = "*" if name == current else " "
                self._say("%s %-4s %g×%gcm" % (bullet, name, wcm, hcm))
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
                self._err(
                    "custom takes width + height in cm (0.1-500) — e.g. '/format custom 21 29.7'"
                )
                return
            self._say("page format custom (%g×%gcm)" % (wcm, hcm))
            return
        try:
            self._editor.set_page_format(parts[0])
        except ValueError:
            self._err("unknown page format '%s' — type '/format' to list formats"
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
            self._err("/save needs a path here (server push is not supported in the Python REPL)")
            return
        if arg.lower().endswith(".stencil"):
            # A `.stencil` path saves the whole project (image + layout + metadata),
            # like the Zig console. The conversation rides along under the `chat`
            # key only while /chat is on (contract §12, opt-in).
            ed = self._editor
            ed.save_chats = self._chat_on
            if self._chat_on and self._chat is not None and self._chat.history:
                ed.attach_chat(self._chat)
            ed.save_project(arg)
            w, h = ed.image_size
            self._say("saved project %s (%dx%d)" % (arg, w, h))
            return
        path, fmt = _resolve_output(arg)
        img = self._editor.save(path, fmt)
        self._report_wrote(path, img.width, img.height)

    def _report_wrote(self, path: str, w: int, h: int) -> None:
        """The canonical output-report line shared by /save and /prompt variants
        (the Zig CLI's ``wrote {path} ({w}x{h})`` stderr contract)."""
        self._say("wrote %s (%dx%d)" % (path, w, h))

    # ── LLM assistant (llm-contract.md) ──
    def _llm_server_conn(self):
        """The live /connect-ed ServerConnection the stencil-server provider reuses.

        A configured server_url must match a connected server; an empty server_url
        falls back to the first connected server. None when not applicable — the
        client then authenticates with whatever token the config carries (none).
        """
        if self._llm.provider != "stencil-server":
            return None
        target = self._llm.server_url
        try:
            if target:
                return self._manager.get(target)
            urls = self._manager.connections
            return self._manager.get(urls[0]) if urls else None
        except ValueError:
            return None

    def _llm_client(self) -> LlmClient:
        """Build the provider client for the session's current /llm config."""
        conn = self._llm_server_conn()
        if conn is not None:
            return LlmClient(self._llm, server=conn)
        return LlmClient(self._llm)

    def _current_png(self) -> bytes:
        """Encode the working image to PNG, reusing the previous encode while the
        editor's public ``revision`` (bumped on every mutation — its documented role
        as a render-cache key) is unchanged. The editor itself is part of the key so
        /drop's fresh Editor can't collide with the old one's revision numbers."""
        ed = self._editor
        cache = self._png_cache
        if cache is not None and cache[0] is ed and cache[1] == ed.revision:
            return cache[2]
        data = ed.result().encode("png")
        self._png_cache = (ed, ed.revision, data)
        return data

    def _edge_map_png(self) -> bytes:
        """§7 edge map: the current render run through the core contour op, encoded
        with the same PNG writer as the working snapshot."""
        from .core import get_core

        img = self._editor.result()
        get_core().apply_contour(img.data, img.width, img.height)
        return img.encode("png")

    def _cmd_prompt(self, arg: str) -> None:
        """/prompt <text>: one LLM turn — print the reply, run the plan's actions on
        the session's editor, write each variant as ``variant-<label>.png``."""
        if not arg:
            self._err("/prompt needs text to send")
            return
        # Answering the last `ask` card by number (contract §11.4). "2" or "1,3" becomes the
        # option LABELS, so what reaches the model is the same text every other surface sends;
        # anything else is an ordinary prompt and simply retires the card, since an
        # unanswered question must never block the conversation.
        answer = ask_answer_text(self._ask, arg)
        if self._ask is not None:
            self._ask = None
        if answer is not None:
            self._say("→ %s" % answer)
            arg = answer
        # §7 auto-continuation: a plan that made a picture and drew no layout cannot
        # have finished the looking-work — the attachment rode along before it existed. The
        # turn is re-sent ONCE with the new image, restating the request (with /chat off
        # there is no history to carry it). Whatever happens, this turn owns the /upload
        # set: the next /upload starts a new one (§2.1).
        try:
            text = arg
            for round_no in range(2):
                if self._prompt_round(text) is not True:
                    return
                # The note is CONTINUATION_NOTE so this writer and the §12.1 gate that
                # refuses it in the persisted document can never drift apart.
                text = "%s\n\n%s" % (arg, CONTINUATION_NOTE)
        finally:
            self._consume_attachments()
            # §10 clearChat runs LAST: after the plan's other actions and any
            # §7 continuation round settled.
            self._confirm_clear_chat()

    def _console_context(self) -> str:
        """The §4 dynamic console-context suffix (the cli console's rule): connection
        URLs only — tokens have no field to ride in — the active project, and each
        server's project names fetched best-effort (an unreachable server just omits
        its listing)."""
        active_url = getattr(self._remote[0], "base", "") if self._remote is not None else ""
        servers: List[ConsoleServer] = []
        for url in self._manager.connections:
            conn = self._manager.get(url)
            names: Optional[List[str]] = None
            if conn is not None:
                try:
                    names = [
                        p.get("name", "?") if isinstance(p, dict) else str(p)
                        for p in conn.list_projects()
                    ]
                except Exception:  # noqa: BLE001 - unreachable ≠ empty: line omitted
                    names = None
            servers.append(ConsoleServer(url=url, active=url == active_url, projects=names))
        active = self._editor.name if self._remote is not None else ""
        return console_context(servers, active)

    def _prompt_round(self, arg: str):
        """One model round. True = the plan only LOADED an image, so the caller should
        re-send once with it attached; anything else = the turn is finished."""
        images: List[Tuple[str, bytes]] = []
        transient: List[Tuple[str, bytes]] = []
        # The system prompt is §4 + the console settings-op block; its dynamic suffix
        # carries the console context always, plus the §7 edge-map sentence when the
        # edge map actually rides (the cli console's suffix order).
        suffix_parts: List[str] = [self._console_context()]
        if self._editor.has_image():
            # Attach the current image for vision, unless it encodes too large.
            data = self._current_png()
            if len(data) > _PROMPT_IMAGE_LIMIT:
                self._note(
                    "current image is %.1f MiB encoded — sending text only "
                    "(limit %d MiB)"
                    % (len(data) / (1024.0 * 1024.0), _PROMPT_IMAGE_LIMIT // (1024 * 1024))
                )
            else:
                images.append(("image/png", data))
                # §7 edge map: rides directly after the snapshot, current turn
                # only. Over the limit alone ⇒ just it is dropped, silently.
                edge = self._edge_map_png()
                if len(edge) <= _PROMPT_IMAGE_LIMIT:
                    transient.append(("image/png", edge))
                    suffix_parts.append(EDGE_MAP_SUFFIX)
        # §2.1: when this turn /upload-ed SEVERAL images they all ride along after the
        # working snapshot (and its edge map), in upload order — that order is what an
        # `image` op indexes (the snapshot itself does not count). They ride transient
        # in chat mode, like the cli console's text-only history: never replayed.
        if len(self._attachments) > 1:
            transient.extend((mt, data) for mt, data, _label in self._attachments)
        system = "%s\n\n%s" % (CONSOLE_SYSTEM_PROMPT, "\n\n".join(suffix_parts))
        try:
            client = self._llm_client()
            if self._chat_on:
                # /chat on: turns accumulate in a session Chat, whose bounded history
                # is replayed on every call. The client is refreshed each turn so
                # in-session /llm changes keep applying.
                if self._chat is None:
                    self._chat = Chat(client)
                else:
                    self._chat.client = client
                _reply, plan = self._chat.send(
                    arg, images=images, system=system, transient_images=transient
                )
            else:
                raw = client.chat(
                    [{"role": "user", "text": arg, "images": images + transient}],
                    system,
                )
                plan = parse_op_plan(raw)
            # §10 openUrl guard: the model may only ECHO the user — a URL absent from
            # the user's own messages this conversation fails the whole plan, nothing
            # executes (history counts user turns only; with /chat off there are none).
            history = self._chat.history if self._chat_on and self._chat is not None else []
            blocked = blocked_open_url(plan, history, arg)
            if blocked is not None:
                self._err(
                    'openUrl blocked: "%s" is not a URL you gave in this '
                    "conversation" % blocked
                )
                return False
            outputs = execute_op_plan(
                plan, self._editor, attachments=self._attachments, console=self
            )
        except (LlmError, OSError) as e:
            # Covers plan/validation errors and the stencil-server stopReason
            # truncation/refusal LlmErrors, plus network failures (URLError).
            self._err("%s" % e)
            return False
        self._say(plan.reply)
        variant_outputs = outputs
        # §2.1: each `save` action wrote a project beside the output — report it like
        # /save does, so a multi-image turn says what it kept.
        for path in plan.saved:
            self._say("saved project %s" % path)
        if plan.actions:
            if self._editor.has_image():
                self._say_status_brief("applied %d action(s)" % len(plan.actions))
            else:
                self._say("applied %d action(s)" % len(plan.actions))
            variant_outputs = outputs[1:] if outputs else []
        # Slugs are deduped (a "-2"/"-3"… suffix on a collision) so same-slug
        # variant labels can't overwrite each other's files, like the Zig CLI/mcp.
        for slug, img in zip(variant_slugs(plan.variants), variant_outputs):
            path = "variant-%s.png" % slug
            img.save(path, "png")
            self._report_wrote(path, img.width, img.height)
        # §11: the plan may also ASK. Printed after the edits and remembered, so the next
        # /prompt can answer it by number.
        if plan.ask is not None:
            self._ask = plan.ask
            self._say(format_ask(plan.ask))
            return False
        return _load_only_plan(plan) and self._editor.has_image()

    # ── §10 console-profile op hooks (execute_op_plan calls these via console=self;
    # a returned string is an execution-miss note per §1, None is success) ──
    def plan_connect(self, action: dict) -> Optional[str]:
        """A plan `connect`: resolve ONLY among the session's live connections (§10's
        stance — the model can never introduce a host); anything else is a note."""
        want = action["server"]
        match = resolve_server(self._manager.connections, want)
        if match == "ambiguous":
            return (
                'skipped connect — "%s" matches several of this session\'s servers; '
                "use the full URL" % want
            )
        if match == "none":
            return (
                'skipped connect — "%s" is not a server you connected this session; '
                "run '/connect <url>' yourself" % want
            )
        self._say("already connected to %s" % self._manager.connections[match])
        return None

    def plan_disconnect(self, action: dict) -> Optional[str]:
        """A plan `disconnect`: resolved against the LIVE connections (exact URL, else
        unique host), then through the same path the /disconnect command takes."""
        want = action["server"]
        urls = self._manager.connections
        match = resolve_server(urls, want)
        if match == "ambiguous":
            return (
                'skipped disconnect — "%s" matches several connected servers; '
                "use the full URL" % want
            )
        if match == "none":
            return 'skipped disconnect — not connected to "%s" (\'/connections\' lists them)' % want
        url = urls[match]
        self._detach_remote_on(url)
        self._manager.disconnect(url)
        self._say("disconnected from %s" % url)
        return None

    def plan_delete(self, action: dict) -> Optional[str]:
        """A plan `delete`: the SAME guards + messages as /delete (cli parity — a
        guard rejection prints its error and the plan carries on)."""
        self._cmd_delete(action["path"])
        return None

    def plan_open_url(self, action: dict) -> Optional[str]:
        """§10 openUrl (user-echo pre-checked in _prompt_round): the same load
        /upload <url> performs, synchronous — later actions see the fetched picture.
        `incognito` is not a console concept and is ignored with a note."""
        if action.get("incognito"):
            self._note("incognito is not a console concept — loading normally")
        url = action["url"]
        try:
            self._editor.load(url)
        except (OSError, ValueError, RuntimeError, codecs.CodecError) as e:
            return 'skipped openUrl — could not load "%s" (%s)' % (url, e)
        # A fresh picture detaches any fetched server project; the running
        # conversation survives — the load happened INSIDE it, at the user's ask.
        self._remote = None
        w, h = self._editor.image_size
        self._say('loaded "%s" (%dx%d)' % (self._editor.name, w, h))
        return None

    def plan_clear(self, action: dict) -> Optional[str]:
        """§10 `clear` → the /drop path's state, in place: drop the working image and
        its lines, leaving the session empty. The conversation survives (the §10 stance
        for model-driven clears: image and edits only — clearing the CHAT is the
        confirmed clearChat op)."""
        self._editor.clear()
        self._remote = None
        self._say("dropped the working image")
        return None

    def plan_clear_chat(self, action: dict) -> Optional[str]:
        """§10 `clearChat`: only RECORD the request — the confirm and the clear are
        deferred to the end of the turn (_confirm_clear_chat), never run mid-plan."""
        self._clear_chat_pending = True
        return None

    def _confirm_clear_chat(self) -> None:
        """The deferred §10 clearChat: ask y/N on the console's own input stream; a
        typed yes runs the exact /chat clear path (Chat.clear + the §12 server-side
        `chat` delete); anything else — EOF / non-interactive input included — is a
        "clear canceled" note, never a failed plan."""
        if not self._clear_chat_pending:
            return
        self._clear_chat_pending = False
        self._say("clear this conversation's history? [y/N]")
        line = self._in.readline() if self._in is not None else ""
        if line.strip().lower() in ("y", "yes"):
            self._cmd_chat("clear")
        else:
            self._say("clear canceled")

    def _show_llm(self) -> None:
        """Bare /llm: print the session's provider config (credentials masked)."""
        cfg = self._llm
        self._say("llm provider %s" % cfg.provider)
        self._say("  url    %s" % (cfg.base_url or "(none)"))
        self._say("  model  %s" % (cfg.model or "(default)"))
        self._say("  key    %s" % _mask(cfg.api_key))
        conn = self._llm_server_conn()
        if conn is not None:
            self._say("  server %s (token from live connection)" % conn.base)
        else:
            self._say("  server %s" % (cfg.server_url or "(none)"))

    def _cmd_llm(self, arg: str) -> None:
        """/llm: bare shows the config; ``<key> <value>`` sets an in-session override."""
        parts = arg.split(None, 1)
        if not parts:
            self._show_llm()
            return
        key = parts[0].lower()
        val = parts[1].strip() if len(parts) > 1 else ""
        cfg = self._llm
        if key == "provider":
            if val not in PROVIDERS:
                self._err(
                    "unknown provider '%s' — use %s" % (val, " | ".join(PROVIDERS))
                )
                return
            # set_provider re-fills the provider's default base URL unless the user
            # pinned one with /llm url (set_base_url); everything else carries over.
            cfg.set_provider(val)
            self._say("llm provider %s (url %s)" % (val, cfg.base_url or "-"))
        elif key == "url":
            if not val:
                self._err("/llm url needs a base URL")
                return
            cfg.set_base_url(val)
            self._say("llm url %s" % val)
        elif key == "model":
            cfg.model = val
            self._say("llm model %s" % (val or "(default)"))
        elif key == "key":
            cfg.api_key = val
            self._say("llm key %s" % ("set" if val else "cleared"))
        elif key == "server":
            cfg.server_url = val
            self._say("llm server %s" % (val or "cleared"))
        else:
            self._err(
                "/llm takes provider | url | model | key | server "
                "(bare /llm shows the config)"
            )

    def _cmd_chat(self, arg: str) -> None:
        """/chat: bare/``show`` prints the mode + turn count; ``on``/``off`` toggles
        §12 chat persistence for the session (default OFF — /prompt stays
        single-turn); ``clear`` empties the conversation (and best-effort drops the
        active remote project's server `chat` file while the mode is on)."""
        sub = arg.strip().lower()
        if sub in ("", "show"):
            turns = len(self._chat.history) if self._chat is not None else 0
            self._say("chat %s (%d message(s))" % ("on" if self._chat_on else "off", turns))
        elif sub == "on":
            self._chat_on = True
            self._say("chat on")
            # §12.2: say who can read a saved chat BEFORE one is written anywhere.
            self._say("  saved into the .stencil project on /save; on a server "
                      "project, readable by everyone it is shared with")
        elif sub == "off":
            self._chat_on = False
            self._say("chat off")
        elif sub == "clear":
            if self._chat is not None:
                self._chat.clear()
            # §12: clearing the conversation clears the persisted server copy too.
            if self._chat_on and self._remote is not None:
                conn, pid = self._remote
                try:
                    conn.delete_file(pid, "chat")
                except ServerError as e:
                    self._note("could not delete the server chat (%s)" % e)
            self._say("chat cleared")
        else:
            self._err("/chat takes on | off | clear | show (bare /chat shows the mode)")

    def _restore_remote_chat(self, conn, pid: str) -> None:
        """Best-effort §12 restore of a fetched project's saved `chat` file.

        A missing file, a fetch error, or a document that parses to no messages all
        mean "no saved chat" — never a console error (the contract's forgiving-read
        rule). Restoring never triggers a model call.
        """
        try:
            doc = json.loads(bytes(conn.get_file(pid, "chat")).decode("utf-8"))
        except (ServerError, OSError, ValueError):
            return
        chat = Chat.from_doc(doc, self._llm_client())
        if chat.history:
            self._chat = chat

    def _say_status_brief(self, verb: str) -> None:
        if not self._editor.has_image():
            self._err("no image loaded")
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
        # A trailing `token=<tok>` (the console's name=/key= convention) supplies the
        # bearer, like the desktop's Connect dialog. A server with ADMIN_TOKEN set
        # refuses to mint one, so an existing token is the only way onto it — and the
        # LLM proxy requires that setting.
        urls, token = [], ""
        for word in arg.split():
            if word.startswith("token="):
                token = word[len("token="):]
            else:
                urls.append(word)
        if not urls:
            self._err("/connect needs one or more server URLs")
            return
        for url in urls:
            try:
                self._manager.connect({"url": url, "token": token} if token else url)
                self._say("connected %s" % normalize_url(url))
            except (ServerError, OSError, ValueError) as e:
                self._err("could not connect to %s (%s)" % (url, e))

    def _cmd_disconnect(self, arg: str) -> None:
        """/disconnect [url] — close one connection (or the most recent when omitted),
        mirroring the Zig console's doDisconnect wording."""
        urls = self._manager.connections
        if not urls:
            self._err("no server connections — use '/connect <url>'")
            return
        target = normalize_url(arg) if arg else urls[-1]
        if not self._manager.has(target):
            self._err("not connected to %s" % target)
            return
        self._detach_remote_on(target)
        self._manager.disconnect(target)
        self._say("disconnected from %s" % target)

    def _detach_remote_on(self, url: str) -> None:
        """Forget the active remote project when its server is being dropped."""
        if self._remote is not None and getattr(self._remote[0], "base", "") == url:
            self._remote = None

    def _cmd_delete(self, arg: str) -> None:
        """/delete <x.stencil> (aliases del/remove/rm) — delete a local .stencil project
        file, with the cli console's full guard set and wording (deleteReject)."""
        reject = Editor.delete_reject(arg)
        if reject == "empty":
            self._err("delete needs a .stencil path — e.g. '/delete project.stencil'")
            return
        if reject == "url":
            self._err("delete only removes local files, not URLs")
            return
        if reject == "not_stencil":
            self._err("delete only removes .stencil project files (got '%s')" % arg)
            return
        if reject == "traversal":
            self._err(
                "refusing to delete a path that escapes the working directory: '%s'" % arg
            )
            return
        try:
            Editor.delete_project(arg)
        except OSError as e:
            self._err("could not delete %s (%s)" % (arg, e))
            return
        self._say("deleted %s" % arg)

    def _cmd_connections(self) -> None:
        conns = self._manager.connections
        if not conns:
            self._say("no server connections — use '/connect <url>'")
            return
        for url in conns:
            self._say(url)

    def _cmd_projects(self, arg: str) -> None:
        url = arg.strip()
        if url:
            conn = self._manager.get(url)
            if conn is None:
                self._err("not connected to %s" % url)
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
            self._err("/fetch needs a project name")
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
                    self._image_replaced()
                    # Track the active remote project for /chat clear's server-side
                    # delete; restore its saved chat only while /chat is on (§12).
                    self._remote = (conn, proj["id"])
                    if self._chat_on:
                        self._restore_remote_chat(conn, proj["id"])
                    w, h = self._editor.image_size
                    self._say('fetched "%s" (%dx%d)' % (name, w, h))
                    return
        self._err('no server project named "%s"' % name)


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
        if args.source_site is not None:
            return _run_scrape(args, err)
        return _run_pipeline(args, err)
    except ServerError as e:
        emit_error(err, "%s" % e)
        return 1
    except (ValueError, RuntimeError, OSError, codecs.CodecError) as e:
        emit_error(err, "%s" % e)
        return 1


if __name__ == "__main__":
    sys.exit(main())
