"""The launch URI (contracts/launch/launch-uri.md): the browser app's ``?key=value`` query, the
same pairs in its ``#fragment``, or a ``stencil://open?…`` link, read into one dict shaped like
browser/js/core/launch/query.js's result. Pure; the words are the package's copy of
``common/config/launchQuery.json`` and the
corpus is common/fixtures/deepLink/launchQuery.json.
"""

from __future__ import annotations

import importlib.resources
import json
import math
import re
import urllib.parse

from ..._ffi.types import NoneType

# The parsed query (§4 of the contract) and one decoded `key=value`.
Query = dict
Pair = tuple[str, str]

_GRAMMAR = json.loads(
  importlib.resources.files("pystencil").joinpath("_data/launchQuery.json").read_text(encoding="utf-8"))
_CANONICAL = {word: key for key, words in _GRAMMAR["keys"].items() for word in words}
SECRET_KEYS = tuple(_GRAMMAR["secretKeys"])
_THEME_WORDS = _GRAMMAR["themeWords"]
_CHAT_DOCKS = tuple(_GRAMMAR["chatDocks"])
_TRUE_WORDS = tuple(_GRAMMAR["trueWords"])
_FALSE_WORDS = tuple(_GRAMMAR["falseWords"])
_UNSAFE_KEYS = ("__proto__", "constructor", "prototype")
# LAUNCH.scriptMaxChars (common/config/constants.json), pinned by tests/test_canonical_drift.py.
SCRIPT_MAX_CHARS = 200000
_NUMBER_SHAPE = re.compile(r"-?\d+(\.\d+)?")
_HTTP = re.compile(r"^https?://", re.IGNORECASE)
_PROJECT_FIELDS = (("description", "description"), ("keywords", "keywords"),
          ("project-color", "color"), ("expire", "expire"))
_LLM_FIELDS = (("llm-provider", "provider"), ("llm-model", "model"), ("llm-base-url", "baseUrl"),
        ("llm-server-url", "serverUrl"), ("llm-api-key", "apiKey"))


def _kebab(key: str) -> str:
  """``mainColor``, ``main_color`` and ``main-color`` are one key."""
  t = re.sub(r"([a-z0-9])([A-Z])", r"\1-\2", key.strip().replace("_", "-"))
  return t.lower().strip("-")


def _camel(key: str) -> str:
  return re.sub(r"-([a-z0-9])", lambda m: m.group(1).upper(), key)


def _flag(value: str) -> bool:
  """A bare flag, a 1 or a true word is on."""
  s = value.strip().lower()
  return s in ("", "1") or s in _TRUE_WORDS


def coerce(value: str) -> (bool | int | float | str):
  """Booleans by word, numbers by shape, the rest as typed: what a console setter would get."""
  s = value.strip()
  low = s.lower()
  if low in _TRUE_WORDS: return True
  if low in _FALSE_WORDS: return False
  if _NUMBER_SHAPE.fullmatch(s): return float(s) if "." in s else int(s)
  return value


def _text(value: (str | NoneType)) -> (str | NoneType):
  t = (value or "").strip()
  return t or None


def _number_or(value: (str | NoneType), fallback: (float | NoneType)) -> (float | NoneType):
  """JS ``Number(v)``: a finite number, else ``fallback``."""
  t = _text(value)
  if t is None: return fallback
  try:
    n = float(t)
  except ValueError:
    return fallback
  return n if math.isfinite(n) else fallback


def _size_or(value: (str | NoneType)) -> (float | NoneType):
  n = _number_or(value, None)
  return n if n is not None and n > 0 else None


def _json_object(text: str) -> (dict | NoneType):
  try:
    data = json.loads(text)
  except ValueError:
    return None
  return data if isinstance(data, dict) else None


def is_launch_query_hash(hash_: str) -> bool:
  """``#stencil=`` and ``#stencil-desktop=`` are the JSON hand-offs; any other ``#a=b`` is this grammar."""
  h = hash_ or ""
  return h.startswith("#") and "=" in h and not h.startswith("#stencil=") and not h.startswith("#stencil-desktop=")


def _pairs(raw: str) -> list[Pair]:
  return [(_kebab(k), v) for k, v in urllib.parse.parse_qsl(raw, keep_blank_values=True, errors="replace")]


def _project_locator(value: str, server_key: (str | NoneType)) -> (dict | NoneType):
  """``host[:port]/projects/<ref>`` names the server too; ``server=`` supplies a missing one."""
  at = value.find("/projects/")
  origin = None if at < 0 else value[:at].rstrip("/")
  ref = (value if at < 0 else value[at + len("/projects/"):]).rstrip("/")
  if not ref: return None
  return {"kind": "project", "server": origin or server_key or None, "ref": ref}


def empty_query() -> Query:
  """The query that names nothing; a corpus vector's ``expect`` is merged over it."""
  return {
    "picture": None, "name": None, "layout": None, "incognito": False, "script": "",
    "theme": None, "settings": {}, "llm": {}, "chat": {"open": False, "dock": None},
    "connect": [], "project": {"description": None, "keywords": None, "color": None, "expire": None},
    "warnings": [], "fromHash": False, "opensSession": False,
  }


def parse_parts(search: str = "", hash_: str = "") -> (Query | NoneType):
  """None when neither carrier has a pair of this grammar; a bare ``?open=<id>`` is not one."""
  pairs = _pairs(search[1:] if search.startswith("?") else search)
  from_hash = is_launch_query_hash(hash_)
  query_len = len(pairs)
  if from_hash: pairs += _pairs(hash_[1:])
  if not any(k != "open" for k, _ in pairs): return None

  q = empty_query()
  q["fromHash"] = from_hash
  kept: list[Pair] = list()

  def warn(code: str, key: str) -> None: q["warnings"].append({"code": code, "key": key})
  def first(key: str) -> (str | NoneType): return next((v for k, v in kept if k == key), None)
  def has(key: str) -> bool: return any(k == key for k, _ in kept)

  for i, (key, value) in enumerate(pairs):
    canonical = _CANONICAL.get(key)
    if canonical is None:
      camel = _camel(key)
      if camel and camel not in _UNSAFE_KEYS and camel not in q["settings"]: q["settings"][camel] = coerce(value)
      continue
    if canonical in SECRET_KEYS and i < query_len:
      warn("secret-in-query", canonical)
      continue
    kept.append((canonical, value))

  server, ident, source = _text(first("server")), _text(first("id")), _text(first("source"))
  if server and ident:
    q["picture"] = {"kind": "server", "url": server, "id": ident, "version": _number_or(first("version"), 0)}
  elif _text(first("project")):
    q["picture"] = _project_locator(_text(first("project")), server)
    if q["picture"] is None: warn("bad-value", "project")
  elif source:
    if _HTTP.match(source) or source.lower().startswith("data:"):
      q["picture"] = {"kind": "source", "url": source, "frame": _number_or(first("frame"), None)}
    else:
      warn("bad-value", "source")
  elif has("blank"):
    q["picture"] = {"kind": "blank", "color": _text(first("blank")) or "#ffffff",
            "width": _size_or(first("width")), "height": _size_or(first("height"))}
  q["name"] = _text(first("name"))

  raw, layout = _text(first("layout-raw")), _text(first("layout"))
  if raw:
    data = _json_object(raw)
    if data is not None: q["layout"] = {"data": data}
    else: warn("bad-json", "layout-raw")
  elif layout:
    if layout.startswith("{"):
      data = _json_object(layout)
      if data is not None: q["layout"] = {"data": data}
      else: warn("bad-json", "layout")
    elif _HTTP.match(layout): q["layout"] = {"url": layout}
    else: warn("bad-value", "layout")

  q["incognito"] = has("incognito") and _flag(first("incognito"))
  script = first("script") or ""
  if len(script) > SCRIPT_MAX_CHARS: warn("bad-value", "script")
  else: q["script"] = script
  if has("theme"):
    mode = _THEME_WORDS.get(first("theme").strip().lower())
    if mode: q["theme"] = mode
    else: warn("bad-value", "theme")
  if _text(first("main-color")): q["settings"]["mainTheme"] = _text(first("main-color"))
  for key, field in _PROJECT_FIELDS:
    if _text(first(key)): q["project"][field] = _text(first(key))
  for key, field in _LLM_FIELDS:
    if _text(first(key)): q["llm"][field] = _text(first(key))
  if has("chat"): q["chat"]["open"] = _flag(first("chat")) or first("chat").strip().lower() == "open"
  if has("chat-dock"):
    dock = first("chat-dock").strip().lower()
    if dock in _CHAT_DOCKS: q["chat"]["dock"] = dock
    else: warn("bad-value", "chat-dock")
  tokens = [v for k, v in kept if k == "token"]
  for n, url in enumerate(v for k, v in kept if k == "connect"):
    if _text(url): q["connect"].append({"url": _text(url), "token": tokens[n] if n < len(tokens) else ""})
  q["opensSession"] = bool(q["picture"]) or q["incognito"] or bool(q["script"])
  return q


def is_actionable(q: (Query | NoneType)) -> bool:
  """A picture, connection, layout, look, assistant or chat key: a picture URL with grammar-looking parameters is not a link."""
  return bool(q) and (q["opensSession"] or bool(q["connect"]) or bool(q["layout"]) or bool(q["theme"])
            or bool(q["llm"]) or q["chat"]["open"] or bool(q["chat"]["dock"]))


def project_origin(q: Query, server: (str | NoneType)) -> (str | NoneType):
  """A project ref's server: the locator's, else the launch's first connection, else None (a local project)."""
  return server or (q["connect"][0]["url"] if q["connect"] else None)


def parse(url: str) -> (Query | NoneType):
  """A whole ``http(s)://…?query#fragment`` or ``stencil://open?…``; None unless actionable."""
  u = urllib.parse.urlsplit((url or "").strip())
  if u.scheme.lower() not in ("http", "https", "stencil"): return None
  q = parse_parts("?" + u.query if u.query else "", "#" + u.fragment if u.fragment else "")
  return q if is_actionable(q) else None
