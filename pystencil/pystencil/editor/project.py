"""Portable ``.stencil`` project files: save, open, delete, and the §12 chat block
they may carry.
"""

from __future__ import annotations

import base64
import binascii
import json
import os

from .._ffi.types import NoneType
from ..llm import MAX_HISTORY, chat_display_text
from ..scriptpaths import is_url
from ._snapshot import _BASE64_PREFIX, _EXT_MIME, _clean_keywords

def _valid_chat_doc(doc) -> (dict | NoneType):
  """A §12.1-clean copy of ``doc``, or None: version 1 with a ``messages`` list, each turn gated by
  :func:`pystencil.llm.chat_display_text` on the way in AND out, so a dirty block never
  round-trips. Never raises — a malformed block means "no saved chat"."""
  if not isinstance(doc, dict) or doc.get("version") != 1: return None
  messages = doc.get("messages")
  if not isinstance(messages, list): return None
  kept: list[dict] = list()
  for m in messages:
    if not isinstance(m, dict): continue
    role, text = m.get("role"), m.get("text")
    if role not in ("user", "assistant") or not isinstance(text, str): continue
    shown = chat_display_text(role, text)
    if shown is not None: kept.append({"role": role, "text": shown})
  return dict(doc, messages=kept[-MAX_HISTORY:])


class _ProjectApi:
  def save_project(self, path: str) -> str:
    """Write the project (ORIGINAL image + export layout + metadata) as one portable ``.stencil`` file; returns the path."""
    orig = self._require_original()
    # Embed the untouched source bytes verbatim (lossless); only re-encode from pixels for a
    # synthetic original (blank / an in-memory Image) that has none.
    if self._source_bytes is not None:
      image_bytes = self._source_bytes
      ext = self._source_ext or "png"
    else:
      image_bytes = orig.encode("png")
      ext = "png"
    mime = _EXT_MIME.get(ext, "image/png")
    doc: dict = {
      "format": "stencil-project",
      "version": 1,
      "name": self._name or "Untitled",
    }
    if self._color: doc["color"] = self._color
    if self._keywords: doc["keywords"] = list(self._keywords)
    if self._source: doc["source"] = self._source
    if self._resource: doc["resource"] = self._resource
    if self.save_chats and _valid_chat_doc(self.chat_doc) and self.chat_doc["messages"]:
      doc["chat"] = self.chat_doc
    doc["image"] = {
      "dataUrl": "data:%s;base64,%s" % (mime, base64.b64encode(image_bytes).decode("ascii")),
      "ext": ext,
      "w": orig.width,
      "h": orig.height,
    }
    doc["layout"] = self.layout().to_dict()
    with open(path, "w", encoding="utf-8") as fh:
      fh.write(json.dumps(doc, indent=2))
    return path

  def open_project(self, src) -> "Editor":
    """Load a ``.stencil`` project (path, JSON ``bytes``/``str``, or ``dict``) — image + layout + metadata — into this editor; returns self. A ``theme`` block is ignored; a saved ``chat`` block lands on :attr:`chat_doc`."""
    if isinstance(src, dict):
      doc = src
    else:
      if isinstance(src, (bytes, bytearray)):
        text = bytes(src).decode("utf-8")
      elif isinstance(src, str) and src.lstrip().startswith("{"):
        text = src  # a JSON string, not a path
      elif isinstance(src, str):
        with open(src, "r", encoding="utf-8") as fh:
          text = fh.read()
      else:
        raise TypeError("unsupported project source: %r" % type(src))
      doc = json.loads(text)
    if not isinstance(doc, dict) or doc.get("format") != "stencil-project":
      raise ValueError("not a Stencil project file")
    version = doc.get("version", 0)
    if not isinstance(version, int) or version < 1:
      raise ValueError("unrecognized project-file version")
    if version > 1:
      raise ValueError("this project needs a newer pystencil (file version %d)" % version)
    image = doc.get("image")
    if not isinstance(image, dict): raise ValueError("project file has no embedded image")
    data_url = image.get("dataUrl", "")
    idx = data_url.find(_BASE64_PREFIX)
    if idx < 0: raise ValueError("project file has no embedded image")
    try:
      image_bytes = base64.b64decode(data_url[idx + len(_BASE64_PREFIX) :])
    except (binascii.Error, ValueError):
      # Match every other bad-input path here (and C#/Zig): a malformed payload is a ValueError.
      raise ValueError("project file has a malformed embedded image") from None
    self.load(
      image_bytes,
      name=doc.get("name") or "Untitled",
      source=doc.get("source"),
      resource=doc.get("resource"),
    )
    # Keep the embedded original verbatim (authoritative ext from the doc) so a re-save is lossless.
    self._source_bytes = image_bytes
    self._source_ext = (image.get("ext") or self._source_ext or "png").lower()
    self._color = doc.get("color") or ""
    self._keywords = _clean_keywords(doc.get("keywords"))
    self.chat_doc = _valid_chat_doc(doc.get("chat"))
    layout = doc.get("layout")
    if isinstance(layout, dict): self.apply_layout(layout)
    return self

  def attach_chat(self, chat_or_doc) -> "Editor":
    """Attach a :class:`pystencil.llm.Chat` (via ``to_doc()``) or a §12.1 dict for
    :meth:`save_project`, written only while :attr:`save_chats` is on; an invalid one attaches nothing."""
    doc = chat_or_doc.to_doc() if hasattr(chat_or_doc, "to_doc") else chat_or_doc
    self.chat_doc = _valid_chat_doc(doc)
    return self

  @staticmethod
  def delete_reject(path) -> (str | NoneType):
    """Which guard (if any) blocks deleting ``path`` — the CLI console's
    ``deleteReject`` ported verbatim: ``"empty"`` / ``"url"`` / ``"not_stencil"`` /
    ``"traversal"``, or ``None`` when the path is deletable. Pure (no I/O), so the
    REPL and the LLM ``delete`` op share one guard order with the API below."""
    if not isinstance(path, str) or not path: return "empty"
    if is_url(path):
      return "url"  # URLs aren't local files
    if not path.lower().endswith(".stencil"):
      return "not_stencil"  # scoped to project files, not a general rm
    # No escaping the working directory (parity with the CLI's hasParentTraversal).
    if ".." in path.replace("\\", "/").split("/"): return "traversal"
    return None

  @staticmethod
  def delete_project(path: str) -> str:
    """Delete a local ``.stencil`` file and return its path, behind the CLI's ``deleteReject``
    guards: no URL, no other extension, no ``..`` component. A loaded project stays loaded.
    Raises ``ValueError`` for a rejected path, ``FileNotFoundError`` for a missing one."""
    reject = _ProjectApi.delete_reject(path)
    if reject == "url":
      raise ValueError("delete_project only removes local files, not URLs: %r" % (path,))
    if reject == "traversal":
      raise ValueError(
        "refusing to delete a path that escapes the working directory: %r" % (path,)
      )
    if reject is not None:
      raise ValueError("delete_project only removes .stencil files: %r" % (path,))
    os.remove(path)
    return path
