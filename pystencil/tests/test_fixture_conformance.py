"""Cross-surface fixture-conformance walkers (pystencil side).

Walks the shared, language-neutral fixture corpus under browser/js/config/
(see each family's _schema.md) through pystencil's REAL entry points and pins
the current behavior. Measured pystencil-vs-corpus divergences live in
tests/fixture_overrides.json — the shared fixtures are never edited here.
"""

from __future__ import annotations

import base64
import io
import json
import math
import re
import struct
import sys
import unittest
import urllib.error
import zlib
from pathlib import Path

# Make the package importable when running `python3 -m unittest` from pystencil/.
_PKG_ROOT = Path(__file__).resolve().parent.parent
if str(_PKG_ROOT) not in sys.path:
    sys.path.insert(0, str(_PKG_ROOT))

from pystencil.editor import Editor
from pystencil.layout import (
    DEFAULT_COLOR,
    DEFAULT_FILL_COLOR,
    DEFAULT_LOCKED,
    DEFAULT_POINT_SIZE,
    DEFAULT_STYLE,
    DEFAULT_THICKNESS,
    Layout,
    Line,
)
from pystencil.llm import Chat, LlmClient, LlmConfig, LlmError, _clean_detail, parse_op_plan

# Shared corpus root, __file__-relative: pystencil/tests → repo root → browser.
_FIXTURES = Path(__file__).resolve().parent.parent.parent / "browser" / "js" / "config"
_LLM_FIXTURES = _FIXTURES / "llm" / "fixtures"

with open(Path(__file__).resolve().parent / "fixture_overrides.json", encoding="utf-8") as _fh:
    _OVERRIDES = json.load(_fh)


def _load(path: Path):
    with open(path, encoding="utf-8") as fh:
        return json.load(fh)


def _norm(v):
    """JSON-structural normalization: NaN floats become the string 'NaN'."""
    if isinstance(v, float) and math.isnan(v):
        return "NaN"
    if isinstance(v, dict):
        return {k: _norm(x) for k, x in v.items()}
    if isinstance(v, list):
        return [_norm(x) for x in v]
    return v


def _filled_line_dict(line: Line) -> dict:
    """A parsed Line as the corpus's 'filled' shape (pointColor always present)."""
    d = line.to_dict()
    d.setdefault("pointColor", line.point_color)
    return d


# ── opPlan — parse_op_plan over the shared op-plan corpus ─────────────────────
_OPPLAN_DIR = _LLM_FIXTURES / "opPlan"
_PROFILES = {"editor", "console", "bot", "mcp", "extension", "all"}
_SURFACES = {"browser", "desktop", "cli", "pystencil", "bot", "mcp", "extension"}
# The pystencil console shares the cli's "console" profile.
_MY_PROFILES = ("console", "all")


class TestOpPlanFixtures(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.fixtures = [
            (p.name, _load(p)) for p in sorted(_OPPLAN_DIR.glob("*.json"))
        ]

    def test_corpus_is_well_formed(self):
        # Port of the reference walker's corpus-shape check (opPlanFixtures.test.js).
        self.assertGreaterEqual(len(self.fixtures), 80, "expected a real corpus")
        for fname, fx in self.fixtures:
            with self.subTest(fixture=fname):
                self.assertEqual(fx["name"] + ".json", re.sub(r"^\d+-", "", fname))
                self.assertIsInstance(fx["profiles"], list)
                self.assertTrue(fx["profiles"])
                for p in fx["profiles"]:
                    self.assertIn(p, _PROFILES)
                self.assertIn(fx["expect"], ("valid", "invalid"))
                self.assertIsNotNone(fx["input"])
                if fx["expect"] == "invalid":
                    self.assertTrue(fx.get("reason"), "invalid cases need a reason")
                for surface, verdict in (fx.get("knownDivergence") or {}).items():
                    self.assertIn(surface, _SURFACES)
                    self.assertIn(verdict, ("valid", "invalid"))

    def test_walk(self):
        overrides = _OVERRIDES["opPlan"]
        walked = 0
        for fname, fx in self.fixtures:
            if not any(p in _MY_PROFILES for p in fx["profiles"]):
                continue
            walked += 1
            local = overrides.get(fx["name"], {}).get("verdict")
            want = local or (fx.get("knownDivergence") or {}).get("pystencil") or fx["expect"]
            text = fx["input"] if isinstance(fx["input"], str) else json.dumps(fx["input"])
            with self.subTest(fixture=fname, want=want):
                if want == "valid":
                    plan = parse_op_plan(text)  # must not raise (chat-only counts)
                    self.assertIsInstance(plan.reply, str)
                    self.assertIsInstance(plan.actions, list)
                else:
                    with self.assertRaises(LlmError):
                        parse_op_plan(text)
        self.assertGreaterEqual(walked, 150, "console/all coverage collapsed")


# ── providerWire — LlmClient request building + reply/error extraction ────────
_WIRE_DIR = _LLM_FIXTURES / "providerWire"
_WIRE_PROVIDERS = {"ollama": "ollama", "openai": "openai-compat", "server": "stencil-server"}


def _wire_client(case) -> LlmClient:
    s = case["settings"]
    provider = _WIRE_PROVIDERS[case["provider"]]
    if provider == "stencil-server":
        cfg = LlmConfig(provider=provider, server_url=s["serverUrl"], model=s.get("model", ""))
        return LlmClient(cfg, token=case.get("token", ""))
    cfg = LlmConfig(
        provider=provider, base_url=s["baseUrl"], model=s.get("model", ""),
        api_key=s.get("apiKey", ""),
    )
    return LlmClient(cfg)


def _wire_messages(chat) -> list:
    return [
        {
            "role": m["role"],
            "text": m["text"],
            "images": [(i["mediaType"], i["data"]) for i in m.get("images", [])],
        }
        for m in chat["messages"]
    ]


def _http_error(case) -> urllib.error.HTTPError:
    er = case["errorResponse"]
    body = er["body"]
    raw = body.encode("utf-8") if isinstance(body, str) else json.dumps(body).encode("utf-8")
    return urllib.error.HTTPError(case["expectUrl"], er["status"], "err", {}, io.BytesIO(raw))


class TestProviderWireFixtures(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.cases = []
        for fn in ("ollama.json", "openai.json", "server.json", "httpErrors.json"):
            cls.cases.extend(_load(_WIRE_DIR / fn))
        assert len(cls.cases) >= 20

    def test_request_building(self):
        # The builders are pure — no network, no seam patching needed.
        for case in self.cases:
            with self.subTest(case=case["name"]):
                client = _wire_client(case)
                req = client._build_request(_wire_messages(case["chat"]), case["chat"]["system"])
                self.assertEqual(req.full_url, case["expectUrl"])
                self.assertEqual(req.get_method(), "POST")
                # urllib stores header keys .capitalize()d ("Content-type").
                self.assertEqual(req.get_header("Content-type"), "application/json")
                # Deep equality: field ABSENCE is part of the contract.
                self.assertEqual(json.loads(req.data.decode("utf-8")), case["expectBody"])
                # Header absent in the fixture means it must not be sent.
                self.assertEqual(req.get_header("Authorization"), case.get("expectAuthorization"))

    def test_reply_and_error_extraction(self):
        overrides = _OVERRIDES["providerWire"]
        for case in self.cases:
            with self.subTest(case=case["name"]):
                client = _wire_client(case)
                self.assertNotIn(case["name"], overrides)  # no pinned wire divergences remain
                if "response" in case and "expectReply" in case:
                    self.assertEqual(client._extract_reply(case["response"]), case["expectReply"])
                else:
                    err = case["expectError"]
                    if "response" in case:  # stopReason errors ride a 200 body
                        with self.assertRaises(LlmError) as ctx:
                            client._extract_reply(case["response"])
                    else:
                        with self.assertRaises(LlmError) as ctx:
                            raise LlmClient._error_from(_http_error(case))
                    e = ctx.exception
                    # Kind mapping: pystencil types errors via code/stop_reason.
                    kind = err["kind"]
                    if kind == "disabled":
                        self.assertEqual(e.code, "llmDisabled")
                    elif kind == "truncated":
                        self.assertEqual(e.stop_reason, "max_tokens")
                    elif kind == "refusal":
                        self.assertEqual(e.stop_reason, "refusal")
                    elif kind == "badReply":
                        # Same typed error; pystencil's own phrasing of the message.
                        self.assertIsNone(e.stop_reason)
                        self.assertIn("no reply text", e.message)
                    else:
                        self.assertEqual(kind, "http")
                        self.assertIsNone(e.stop_reason)
                    self.assertEqual(e.status, err.get("status"))
                    # Message: the schema lets a walker substitute its own
                    # sanitizer/extraction output; pystencil only reads the
                    # server-shaped {code,message} error body.
                    if "errorResponse" in case:
                        body = case["errorResponse"]["body"]
                        if isinstance(body, dict) and body.get("message"):
                            want_msg = _clean_detail(body["message"])
                        else:
                            want_msg = "HTTP %d" % case["errorResponse"]["status"]
                        self.assertEqual(e.message, want_msg)
                    else:
                        self.assertTrue(e.message)


# ── sanitizer — _clean_detail over the shared vectors ─────────────────────────
_URL_RE = re.compile(r"[a-z][a-z0-9+.-]*://", re.I)
_TOKEN_RUN_RE = re.compile(r"[A-Za-z0-9_-]{24,}")


class TestSanitizerFixtures(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.cases = _load(_LLM_FIXTURES / "sanitizer" / "cases.json")

    def _divergent_surfaces(self, name: str):
        m = re.match(r"DIVERGENCE\(([^)]*)\)", name)
        return [s.strip() for s in m.group(1).split(",")] if m else []

    def test_walk(self):
        overrides = _OVERRIDES["sanitizer"]
        for case in self.cases:
            name = case["name"]
            with self.subTest(case=name):
                if case["input"] is None:
                    continue  # _clean_detail takes str; callers guard None
                got = _clean_detail(case["input"])
                if name in overrides:
                    want = overrides[name]["expect"]
                elif "pystencil" in self._divergent_surfaces(name):
                    # Recompute locally: Python counts code points, not UTF-16
                    # units. Only the length cap may diverge, so require that no
                    # redaction/control step applies, then re-derive the cut.
                    inp = case["input"]
                    self.assertLessEqual(len(inp), 800)
                    self.assertIsNone(_URL_RE.search(inp))
                    self.assertIsNone(_TOKEN_RUN_RE.search(inp))
                    self.assertIsNone(re.search(r"[\x00-\x1f\x7f]", inp))
                    want = " ".join(inp.split())
                    if len(want) > 200:
                        want = want[:199].strip() + "…"
                else:
                    want = case["expect"]
                self.assertEqual(got, want)
                # Invariants hold for every case, divergent or not.
                self.assertLessEqual(len(got), 200)
                self.assertIsNone(_URL_RE.search(got))
                self.assertIsNone(_TOKEN_RUN_RE.search(got))


# ── chatDoc — Chat.from_doc / Chat.to_doc over the §12.1 vectors ──────────────
def _history_messages(chat: Chat) -> list:
    return [{"role": m["role"], "text": m["text"]} for m in chat.history]


class TestChatDocFixtures(unittest.TestCase):
    def test_roundtrip(self):
        for case in _load(_LLM_FIXTURES / "chatDoc" / "roundtrip.json"):
            with self.subTest(case=case["name"]):
                doc = case["doc"]
                from_str = Chat.from_doc(json.dumps(doc))
                from_obj = Chat.from_doc(doc)
                self.assertEqual(_history_messages(from_str), doc["messages"])
                self.assertEqual(_history_messages(from_obj), doc["messages"])
                # serialize∘parse is the identity (savedAt pinned via now_ms,
                # like the browser's buildChatDoc(parsed.messages, parsed.savedAt)).
                self.assertEqual(from_str.to_doc(now_ms=doc["savedAt"]), doc)

    def test_tolerance(self):
        # Chat.from_doc never surfaces savedAt, so lenient reads are compared at
        # the message level; expectParsed null reads as an empty conversation.
        for case in _load(_LLM_FIXTURES / "chatDoc" / "tolerance.json"):
            with self.subTest(case=case["name"]):
                exp = case["expectParsed"]
                want = [] if exp is None else exp["messages"]
                inp = case["docString"] if "docString" in case else case["doc"]
                self.assertEqual(_history_messages(Chat.from_doc(inp)), want)
                if "doc" in case:  # string-form parity for object inputs
                    self.assertEqual(
                        _history_messages(Chat.from_doc(json.dumps(case["doc"]))), want
                    )


# ── layout — Line/Layout parse + export over the shared vectors ───────────────
_LAYOUT_DIR = _FIXTURES / "fixtures" / "layout"


class TestLayoutFixtures(unittest.TestCase):
    def test_defaults_align_with_corpus(self):
        # The cross-surface per-line defaults pinned by sparse.json/_schema.md.
        self.assertEqual(DEFAULT_COLOR, "#FFFF00")
        self.assertEqual(DEFAULT_THICKNESS, 2.0)
        self.assertEqual(DEFAULT_POINT_SIZE, 4.0)
        self.assertEqual(DEFAULT_STYLE, "solid")
        self.assertEqual(DEFAULT_LOCKED, False)
        self.assertEqual(DEFAULT_FILL_COLOR, "transparent")
        self.assertEqual(Line().point_color, "")  # '' = inherit stroke

    def test_sparse_filled(self):
        # pystencil is a tolerant parser: its parse of the sparse lines must
        # yield the corpus's expectFilled shape (empty-points lines kept).
        overrides = _OVERRIDES["layout/sparse"]
        for case in _load(_LAYOUT_DIR / "sparse.json"):
            with self.subTest(case=case["name"]):
                raw = case["sparse"]
                lines = [Line.from_dict(x) for x in raw] if isinstance(raw, list) else []
                got = [_filled_line_dict(ln) for ln in lines]
                want = overrides.get(case["name"], {}).get("expectFilled", case["expectFilled"])
                self.assertEqual(_norm(got), _norm(want))

    def test_payload_export(self):
        # Layout.from_dict → to_dict is pystencil's export path; uses imageFilter
        # (aligned with the browser) and pins the top-level key order.
        overrides = _OVERRIDES["layout/payload"]
        for case in _load(_LAYOUT_DIR / "payload.json"):
            with self.subTest(case=case["name"]):
                out = Layout.from_dict(case["layout"]).to_dict()
                over = overrides.get(case["name"])
                want = over["expectPayload"] if over else case["expectPayload"]
                self.assertEqual(_norm(out), _norm(want))
                order = case.get("expectKeyOrder")
                if order and not over:
                    self.assertEqual(list(out.keys()), order)
                for key in case.get("forcedKeys", []):
                    self.assertIn(key, out)


# ── stencilProject — Editor.open_project over the .stencil vectors ────────────
_PROJECT_DIR = _FIXTURES / "fixtures" / "stencilProject"


def _png_1x1() -> bytes:
    def chunk(tag: bytes, data: bytes) -> bytes:
        return (
            struct.pack(">I", len(data)) + tag + data
            + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)
        )

    ihdr = struct.pack(">IIBBBBB", 1, 1, 8, 2, 0, 0, 0)
    idat = zlib.compress(b"\x00\xff\x00\x00")
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) + chunk(b"IDAT", idat) + chunk(b"IEND", b"")


_PNG_B64 = base64.b64encode(_png_1x1()).decode("ascii")


def _fill_line(sparse: dict) -> dict:
    d = {
        "points": sparse.get("points", []),
        "color": DEFAULT_COLOR,
        "thickness": DEFAULT_THICKNESS,
        "pointSize": DEFAULT_POINT_SIZE,
        "style": DEFAULT_STYLE,
        "locked": DEFAULT_LOCKED,
        "fillColor": DEFAULT_FILL_COLOR,
        "pointColor": "",
    }
    d.update({k: v for k, v in sparse.items() if k in d})
    return d


class TestStencilProjectFixtures(unittest.TestCase):
    def test_valid(self):
        # pystencil's reader decodes pixels (unlike the browser's format-only
        # parser), so the stub payload is swapped for a real 1x1 PNG; the file's
        # format-level content is untouched. Input is fed as bytes so a JSON
        # string is never mistaken for a path.
        overrides = _OVERRIDES["stencilProject"]
        for case in _load(_PROJECT_DIR / "valid.json"):
            with self.subTest(case=case["name"]):
                text = re.sub(r"base64,[A-Za-z0-9+/=]*", "base64," + _PNG_B64, case["file"])
                if overrides.get(case["name"], {}).get("verdict") == "error":
                    with self.assertRaises(ValueError):
                        Editor().open_project(text.encode("utf-8"))
                    continue
                ed = Editor().open_project(text.encode("utf-8"))
                raw = json.loads(case["file"])
                want = case["project"]
                # Name/color are kept verbatim (browser trims/normalizes).
                self.assertEqual(ed.name, raw.get("name") or "Untitled")
                self.assertEqual(ed.name.strip(), want["name"])
                self.assertEqual(ed.project_color, raw.get("color") or "")
                self.assertEqual(
                    ed.project_color.lstrip("#").lower(), want["color"].lstrip("#").lower()
                )
                self.assertEqual(ed.keywords, want["keywords"])
                self.assertEqual(ed._source or "", want["source"])
                self.assertEqual(ed._resource or "", want["resource"])
                # ext: the doc's value verbatim, lowercased (browser re-validates it).
                self.assertEqual(
                    ed._source_ext, ((raw.get("image") or {}).get("ext") or "png").lower()
                )
                # blank/blankColor/theme have no pystencil representation — skipped.
                snap = ed._current()
                lay = want["layout"]
                got_lines = [_filled_line_dict(ln) for ln in snap.lines]
                self.assertEqual(_norm(got_lines), _norm([_fill_line(x) for x in lay["lines"]]))
                self.assertEqual(snap.rotation, lay.get("rotationQuarters") or 0)
                self.assertEqual(snap.filter_mode, lay.get("imageFilter") or "")
                self.assertEqual(snap.filter_color, lay.get("filterColor") or "")
                self.assertEqual(ed._page_size, lay.get("pageSize") or "")
                cr = lay.get("cropRect")
                if cr is None:
                    self.assertIsNone(snap.crop)
                else:
                    # Read-both since Phase 6: canonical w/h wins, legacy
                    # width/height still reads — the corpus's w/h rects adopt.
                    self.assertEqual(
                        snap.crop,
                        (
                            cr.get("x", 0),
                            cr.get("y", 0),
                            cr.get("w", cr.get("width", 0)),
                            cr.get("h", cr.get("height", 0)),
                        ),
                    )

    def test_invalid(self):
        # Every corpus error case must fail here too; match on the CASE, not the
        # browser's message. dataUrl-non-string raises AttributeError (pinned).
        for case in _load(_PROJECT_DIR / "invalid.json"):
            with self.subTest(case=case["name"]):
                with self.assertRaises((ValueError, AttributeError)):
                    Editor().open_project(case["file"].encode("utf-8"))


if __name__ == "__main__":
    unittest.main()
