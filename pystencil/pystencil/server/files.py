from __future__ import annotations

"""The file half of a connection's REST surface, plus high-level remote sync.

A mixin over ``_request`` (owned by :class:`ServerConnection`). The server is
codec-free, so every upload carries its dimensions and extension in the query while
the pixel bytes go in an octet-stream body. The sync half ports ``remoteSync.js``.
"""

import urllib.parse
from typing import Any


class _FileApi:
  """Per-project file bytes and the create/save remote-project flows."""

  def get_file(self, pid: str, kind: str) -> bytes:
    """GET /projects/{id}/files/{kind} → raw image bytes."""
    path = f"/projects/{urllib.parse.quote(str(pid))}/files/{urllib.parse.quote(str(kind))}"
    return self._request("GET", path, raw=True)

  def put_file(self, pid: str, kind: str, data: bytes, ext: str, w: int, h: int) -> dict:
    """POST /projects/{id}/files/{kind}?ext&w&h → {path, w, h}.

    The server is codec-free, so dimensions/extension are passed in query
    params while the pixel bytes go in the octet-stream body.
    """
    path = f"/projects/{urllib.parse.quote(str(pid))}/files/{urllib.parse.quote(str(kind))}"
    query = {"ext": ext, "w": str(w), "h": str(h)}
    return self._request("POST", path, body=data, raw=True, query=query)

  def delete_file(self, pid: str, kind: str) -> None:
    """DELETE /projects/{id}/files/{kind} (204 No Content; idempotent).

    Valid only for the filestore-only kinds (`video`, `variantN`, `chat`) —
    the server answers 400 for `original`/`result`, which are part of the
    project record and are removed with the project.
    """
    path = f"/projects/{urllib.parse.quote(str(pid))}/files/{urllib.parse.quote(str(kind))}"
    self._request("DELETE", path)

  # ── high-level sync (remoteSync.js) ──
  def _current_version(self, pid: str, fallback: int) -> int:
    """Re-read a project's version after a file write (which bumps it but
    returns none of its own), mirroring remoteSync.js currentVersion."""
    try:
      proj = self._project_record(pid)
      v = proj.get("version") if proj else None
      return fallback if v is None else int(v)
    except Exception:
      return fallback

  @staticmethod
  def _image_bytes(image: Any) -> tuple[bytes, int, int]:
    """Encode an Image to PNG bytes + dimensions (duck-typed to avoid a
    hard import of pystencil.image)."""
    data = image.encode("png")
    return bytes(data), int(image.width), int(image.height)

  def create_remote_project(
    self,
    name: str,
    image: Any = None,
    source: str | None = None,
    resource: str | None = None,
    description: str | None = None,
    layout: Any = None,
  ) -> dict:
    """Create a project and (when an image is given) upload the original.

    Port of remoteSync.js createRemoteProject: create → putFile('original').
    Returns the created project record with its version refreshed after the
    upload (which bumps it server-side).
    """
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
      data, w, h = self._image_bytes(image)
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
    name: str | None = None,
    color: str | None = None,
  ) -> dict:
    """Version-guarded save-back (layout/name/color) plus optional result upload.

    Port of remoteSync.js saveRemoteProject: update → putFile('result'). A
    409 (lost last-writer-wins race) is surfaced as ServerError(code=
    "conflict"). Returns the refreshed project record. `color` follows the
    same nil-means-unchanged contract as `name`.
    """
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
      data, w, h = self._image_bytes(image)
      if data:
        self.put_file(pid, "result", data, "png", w, h)
        new_version = self._current_version(pid, new_version)
    rec["version"] = new_version
    return rec

