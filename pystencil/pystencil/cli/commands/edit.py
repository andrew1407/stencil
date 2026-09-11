from __future__ import annotations

"""The picture edits and the lines that report them: crop, rotate, filter, layout
and save, plus the status line most of them print.
"""

from typing import List

from ..oneshot import _resolve_output
from ..registry import command


class _EditCommands:
    """Crop, rotate, filter, draw, export, save, history navigation and status."""

    def _cmd_filter_variants(self) -> None:
        """Bare /filter: list the possible modes (a bare required-arg command never errors)."""
        self._say("filters:")
        self._say("  bw        black & white")
        self._say("  sepia     warm sepia tone")
        self._say("  invert    negative colours")
        self._say("  contour   edge outline (dark lines on white)")
        self._say("  none      remove the filter")
        self._say("  <colour>  a colour name/#hex duotone tint")

    @command("crop", usage="/crop <spec> [album]",
             help="crop, e.g. x1=10%% x2=90%% y1=10%% y2=90%%".replace("%%", "%"))
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

    @command("rotate", "rot", "turn", usage="/rotate <int>",
             help="rotate int×90° (aliases: rot, turn)")
    def _cmd_rotate(self, arg: str) -> None:
        self._editor.rotate(int(arg))
        self._say_status_brief("rotated")

    @command(
        "filter",
        usage="/filter <mode>",
        help="bw | sepia | invert | contour | none | colour (also: /bw /sepia /none /tint)",
    )
    def _cmd_filter(self, arg: str) -> None:
        if not arg:
            self._cmd_filter_variants()
            return
        self._editor.apply_filter(arg)
        self._say_status_brief("filtered")

    @command("bw")
    def _cmd_bw(self, arg: str) -> None:
        self._apply_named_filter("bw")

    @command("sepia")
    def _cmd_sepia(self, arg: str) -> None:
        self._apply_named_filter("sepia")

    @command("none")
    def _cmd_none(self, arg: str) -> None:
        self._apply_named_filter("none")

    def _apply_named_filter(self, mode: str) -> None:
        """The /bw, /sepia and /none shortcuts: the verb itself names the mode."""
        self._editor.apply_filter(mode)
        self._say_status_brief("filtered")

    @command("tint", "color", "colour")
    def _cmd_tint(self, arg: str) -> None:
        self._editor.apply_filter(arg)
        self._say_status_brief("filtered")

    @command("apply", "draw", usage="/apply <path|url>",
             help="draw a layout JSON onto the image (alias: draw)")
    def _cmd_apply(self, arg: str) -> None:
        self._editor.draw(arg)
        self._say_status_brief("drew layout")

    @command("layout", "savelayout", "exportlayout", usage="/layout [path]",
             help="EXPORT the structured layout JSON")
    def _cmd_layout(self, arg: str) -> None:
        path = self._editor.save_layout(arg or None)
        self._say("exported layout -> %s" % path)

    @command("save", "write", usage="/save [path]",
             help="write the working image (a .stencil path saves the project)")
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


    @command("undo", "u", usage="/undo /redo /reset", help="walk the edit history")
    def _cmd_undo(self, arg: str) -> None:
        self._say("undid" if self._editor.undo() else "nothing to undo")

    @command("redo", "r")
    def _cmd_redo(self, arg: str) -> None:
        self._say("redid" if self._editor.redo() else "nothing to redo")

    @command("reset", "revert")
    def _cmd_reset(self, arg: str) -> None:
        self._editor.reset()
        self._say("reset to original")

    def _say_status_brief(self, verb: str) -> None:
        if not self._editor.has_image():
            self._err("no image loaded")
            return
        w, h = self._editor.image_size
        self._say("%s -> %dx%d" % (verb, w, h))

    @command("status", "info", "image", usage="/status",
             help="show the working image (alias: info)")
    def _cmd_status(self, arg: str = "") -> None:
        if not self._editor.has_image():
            self._say("no image loaded")
            return
        w, h = self._editor.image_size
        self._say('image "%s" %dx%d' % (self._editor.name, w, h))
