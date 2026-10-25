"""A launch link handed to ``/upload`` (contracts/launch/launch-uri.md): the browser app's URL or a
``stencil://`` link. Connections first, then the picture (a server project by id or name, a picture
URL, a blank), then the layout and the assistant settings, each through the command a typed line
would reach; what this console has no command for is said and left out.
"""

from __future__ import annotations

import argparse
import json
from urllib.parse import urlsplit

from ...server import normalize_url
from .uri import Query, parse, project_origin

_WARNING = {
  "secret-in-query": 'launch link: "%s" is ignored in the ?query — put it in the #fragment',
  "bad-json": 'launch link: "%s" is not a JSON object',
  "bad-value": 'launch link: "%s" has an unusable value',
}
_LEFT_OUT = "launch link: %s is not something this console sets — left out"
_LLM_WORDS = (("provider", "provider"), ("url", "baseUrl"), ("model", "model"),
       ("server", "serverUrl"), ("key", "apiKey"))


def _connect_arg(url: str, token: str) -> str:
  return url + (" token=%s" % token if token else "")


def _token_for(q: Query, origin: str) -> str:
  return next((c["token"] for c in q["connect"] if c["url"].lower() == origin.lower()), "")


def _layout_arg(layout: dict) -> str:
  """What ``/apply`` and ``--layout`` take: the URL, or the inline object as compact JSON."""
  return layout.get("url") or json.dumps(layout["data"], separators=(",", ":"))


def _fetch(repl, q: Query, origin: str, ref: str) -> None:
  """``/fetch <ref> <base>`` once connected to ``origin`` (dialled with the link's token when not yet)."""
  base = normalize_url(origin)
  if not repl._manager.has(base): repl._cmd_connect(_connect_arg(origin, _token_for(q, origin)))
  if not repl._manager.has(base):
    repl._err("launch link: not connected to %s — the picture was left out" % base)
    return
  repl._cmd_fetch("%s %s" % (ref, base))


def _open_source(repl, q: Query, p: dict) -> None:
  frame = p["frame"]
  repl._editor.load(p["url"], frame=int(frame) if frame is not None and frame >= 0 else None)
  repl._image_replaced()
  repl._add_attachment(p["url"])
  w, h = repl._editor.image_size
  repl._say('loaded "%s" (%dx%d)' % (repl._editor.name, w, h))


def _open_blank(repl, q: Query, p: dict) -> None:
  both = p["width"] is not None and p["height"] is not None
  repl._cmd_blank("%d %d %s" % (p["width"], p["height"], p["color"]) if both else p["color"])


def _open_server(repl, q: Query, p: dict) -> None:
  _fetch(repl, q, p["url"], p["id"])


def _open_project_ref(repl, q: Query, p: dict) -> None:
  origin = project_origin(q, p["server"])
  if origin is None:
    repl._err("launch link: project '%s' names no server, and this console keeps no local projects — left out" % p["ref"])
  else:
    _fetch(repl, q, origin, p["ref"])


# Each picture kind lands on the command a typed line would reach.
_OPENERS = dict(source=_open_source, blank=_open_blank, server=_open_server, project=_open_project_ref)


def _open_picture(repl, q: Query) -> None:
  p = q["picture"]
  if p: _OPENERS[p["kind"]](repl, q, p)


def _origin(url: str) -> tuple:
  parts = urlsplit(url or "")
  return parts.scheme.lower(), parts.netloc.lower()


def _apply_llm(repl, llm: dict) -> None:
  """A key belongs to the base URL it was given for (launch-uri.md §2): a link that moves that
  origin drops it unless the link brings its own."""
  was = repl._llm.base_url
  for word, field in _LLM_WORDS:
    if llm.get(field): repl._cmd_llm("%s %s" % (word, llm[field]))
  if not llm.get("apiKey") and repl._llm.api_key and _origin(was) != _origin(repl._llm.base_url):
    repl._llm.api_key = ""
    repl._note("launch link: the assistant now points at %s — the API key held for the old endpoint was dropped"
               % repl._llm.base_url)


def open_link(repl, url: str) -> bool:
  """Apply the link through the REPL's own commands; False when the URL is a plain picture after all."""
  q = parse(url)
  if q is None: return False
  for w in q["warnings"]: repl._note(_WARNING[w["code"]] % w["key"])
  if q["theme"] or q["settings"]: repl._note(_LEFT_OUT % "the look and editor settings")
  if q["chat"]["open"] or q["chat"]["dock"]: repl._note(_LEFT_OUT % "the chat panel")
  if q["script"]: repl._note(_LEFT_OUT % "the script")
  if q["name"]: repl._note(_LEFT_OUT % "the name")
  if any(q["project"].values()): repl._note(_LEFT_OUT % "the project fields")
  for c in q["connect"]: repl._cmd_connect(_connect_arg(c["url"], c["token"]))
  _apply_llm(repl, q["llm"])
  _open_picture(repl, q)
  if q["layout"]:
    if not repl._editor.has_image():
      repl._note(_LEFT_OUT % "the layout (no picture to put it on)")
    else:
      repl._cmd_apply(_layout_arg(q["layout"]))
  return True


def expand_args(args: argparse.Namespace) -> None:
  """A launch URI in ``--input`` (or the positional) becomes the flags it stands for: ``source`` is
  ``-i``, ``frame`` is ``-f``, ``blank`` with its size is ``--blank``, ``layout`` is ``-l``. A link
  naming no picture, or a server one, is left as it is (the one-shot has no server mode)."""
  for attr in ("input", "output"):
    value = getattr(args, attr, None)
    q = parse(value) if isinstance(value, str) else None
    p = q["picture"] if q else None
    if not p or p["kind"] not in ("source", "blank"): continue
    if attr == "output": args.output = None
    if p["kind"] == "source":
      args.input = p["url"]
      if p["frame"] is not None and p["frame"] >= 0: args.frame = int(p["frame"])
    else:
      args.input = None
      both = p["width"] is not None and p["height"] is not None
      args.blank = ([str(int(p["width"])), str(int(p["height"]))] if both else []) + [p["color"]]
    if q["layout"]: args.layout = _layout_arg(q["layout"])
    return
