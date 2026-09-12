from __future__ import annotations

"""Page-shaped commands: /blank and /format."""

from ..blank import BlankSpec
from ..registry import command


class _PageCommands:
  """/blank and /format — the session's page size and the blanks it drives."""
  @command("blank", "new", usage="/blank [f] [w h] [color]",
      help="create a blank page, f = a page format (alias: new)")
  def _cmd_blank(self, arg: str) -> None:
    from ...core import get_core

    core = get_core()
    spec = BlankSpec.from_arg(arg)
    page, width, height, color = spec.page, spec.width, spec.height, spec.color
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

  @command("format", usage="/format [name|custom w h]",
      help="list the page formats / set the session's format")
  def _cmd_format(self, arg: str) -> None:
    """/format: bare lists the formats; a name sets; ``custom <w> <h>`` sets custom."""
    from ...core import get_core

    core = get_core()
    parts = arg.split()
    if not parts:
      # list every named format with its portrait cm size, marking the current one.
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
