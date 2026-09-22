"""Getting a picture into the session: /upload, /source-upload and the §2.1 upload set."""

from __future__ import annotations

import urllib.parse

from ..._ffi.types import NoneType
from ... import _net
from ...llm import MAX_UPLOAD_ATTACHMENTS, Chat
from ...sitesource import scan_page
from ..registry import command


class _SourceCommands:
  """/upload, /source-upload and the attachment registry they feed."""
  @command("upload", "open", "load", usage="/upload <path|url>",
      help="load an image or a .stencil project (aliases: open, load)")
  def _cmd_upload(self, arg: str) -> None:
    if not arg:
      self._err("/upload needs a path or URL")
      return
    if arg.lower().endswith(".stencil"):
      # A whole .stencil loads image + layout + metadata at once. A saved `chat` block restores
      # the conversation only while /chat is on (contract §12).
      self._editor.open_project(arg)
      self._image_replaced()
      if self._chat_on and self._editor.chat_doc:
        chat = Chat.from_doc(self._editor.chat_doc, self._llm_client())
        if chat.history: self._chat = chat
    else:
      self._editor.load(arg)
      self._image_replaced()
      # §2.1: the uploads of one turn are its attachments, indexed by an `image` op. Registered
      # as the wire's PNG re-encode.
      self._add_attachment(arg)
    w, h = self._editor.image_size
    self._say('loaded "%s" (%dx%d)' % (self._editor.name, w, h))

  def _add_attachment(self, label: str) -> None:
    """Remember the just-loaded image as one of this turn's §2.1 attachments."""
    if self._attachments_used:
      self._attachments = list()
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
    if self._attachments: self._attachments_used = True

  @command(
    "source-upload", "sourceupload", "scrape",
    usage="/source-upload <url> [index=0] [format=all] [name=] [minW=-1] [maxW=-1]"
       " [minH=-1] [maxH=-1]",
    help="scrape a page, load one filtered image (alias: scrape)",
  )
  def _cmd_source_upload(self, arg: str) -> None:
    """/source-upload <url> [index= format= minW= maxW= minH= maxH=]: scrape a page,
    filter its image-category stills (img/bg/poster — NOT video), and load one."""
    parts = arg.split()
    if not parts:
      self._err("/source-upload needs a URL")
      return
    url = parts[0]
    opts = {"index": 0, "format": "all", "minw": -1, "maxw": -1, "minh": -1, "maxh": -1}
    custom_name: (str | NoneType) = None
    try:
      for tok in parts[1:]:
        if "=" not in tok: continue
        key, val = tok.split("=", 1)
        key = key.strip().lower()
        if key == "format":
          opts["format"] = val.strip() or "all"
        elif key == "name":
          custom_name = val.strip() or None
        elif key in opts: opts[key] = int(val)
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
    from ...sitesource import _sub_strict

    page_host = urllib.parse.urlparse(url).hostname or ""
    data = _net._fetch(item.url, strict=_sub_strict(item.url, page_host))
    # Replace the working image via the session's load path (mirror /upload). A
    # name= token overrides the URL-derived project name.
    self._editor.load(
      bytes(data), name=custom_name or self._editor._name_from_url(item.url)
    )
    self._image_replaced()
    w, h = self._editor.image_size
    self._say('loaded "%s" (%dx%d)' % (self._editor.name, w, h))
