"""The file half of a connection's REST surface, plus remote sync (twin of ``remoteSync.js``).

The server is codec-free, so every upload carries its dimensions and extension in the query
while the pixel bytes go in an octet-stream body.
"""

from __future__ import annotations

import urllib.parse
from typing import Any

from .._ffi.types import NoneType


class _FileApi:
  def get_file(self, pid: str, kind: str) -> bytes:
    """GET /projects/{id}/files/{kind} → raw image bytes."""
    path = f"/projects/{urllib.parse.quote(str(pid))}/files/{urllib.parse.quote(str(kind))}"
    return self._request("GET", path, raw=True)

  def put_file(self, pid: str, kind: str, data: bytes, ext: str, w: int, h: int) -> dict:
    """POST /projects/{id}/files/{kind}?ext&w&h → {path, w, h}."""
    path = f"/projects/{urllib.parse.quote(str(pid))}/files/{urllib.parse.quote(str(kind))}"
    query = {"ext": ext, "w": str(w), "h": str(h)}
    return self._request("POST", path, body=data, raw=True, query=query)

  def delete_file(self, pid: str, kind: str) -> None:
    """DELETE /projects/{id}/files/{kind}, idempotent; only `video`, `variantN` and `chat` —
    the server answers 400 for `original`/`result`, which go with the project."""
    path = f"/projects/{urllib.parse.quote(str(pid))}/files/{urllib.parse.quote(str(kind))}"
    self._request("DELETE", path)

  def _current_version(self, pid: str, fallback: int) -> int:
    """The version after a file write, which bumps it but returns none; `fallback` on error."""
    try:
      proj = self._project_record(pid)
      v = proj.get("version") if proj else None
      return fallback if v is None else int(v)
    except Exception:
      return fallback

  @staticmethod
  def __image_bytes(image: Any) -> tuple[bytes, int, int]:
    data = image.encode("png")
    return bytes(data), int(image.width), int(image.height)

  def create_remote_project(
    self,
    name: str,
    image: Any = None,
    source: (str | NoneType) = None,
    resource: (str | NoneType) = None,
    description: (str | NoneType) = None,
    layout: Any = None,
  ) -> dict:
    """Create a project, then upload `image` as its original; the record's version is refreshed."""
    has_image = image is not None
    rec = self.create_project(
      name=name or "Untitled",
      source=source or "",
      resource=resource or "",
      description=description,
      hasImage=has_image,
      layout=layout,
    )
    rec = rec or {}
    version = rec.get("version", 0)
    if has_image:
      data, w, h = self.__image_bytes(image)
      if data:
        self.put_file(rec["id"], "original", data, "png", w, h)
        rec["version"] = self._current_version(rec["id"], version)
    return rec

  def save_remote_project(
    self,
    pid: str,
    version: int,
    layout: Any,
    image: Any = None,
    name: (str | NoneType) = None,
    color: (str | NoneType) = None,
  ) -> dict:
    """Version-guarded save-back, then the result upload; a 409 is ServerError("conflict").
    None keeps `name` or `color`."""
    try:
      rec = self.update_project(pid, layout=layout, name=name, color=color, version=version)
    except ServerError as err:
      if err.status == 409 or err.code == "conflict":
        raise ServerError(
          "conflict",
          "This project was edited elsewhere — reload it from the "
          "server before saving again.",
          status=err.status,
        ) from None
      raise
    rec = rec or {}
    new_version = rec.get("version", version)
    if image is not None:
      data, w, h = self.__image_bytes(image)
      if data:
        self.put_file(pid, "result", data, "png", w, h)
        new_version = self._current_version(pid, new_version)
    rec["version"] = new_version
    return rec

