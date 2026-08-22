from __future__ import annotations

# Tests for the pystencil command-line front-end (pystencil/cli.py).
#
# These drive cli.main() directly with argument lists (no subprocess), capturing
# stderr to assert the canonical `wrote {path} ({w}x{h})` contract and verifying
# the written artifacts decode/parse correctly. The whole suite self-skips when
# the native core library cannot be loaded, since every pipeline needs it.

import contextlib
import io
import json
import os
import tempfile
import unittest

from pystencil import _severity
from pystencil import cli
from pystencil import codecs
from pystencil import sitesource
from pystencil.editor import Editor
from pystencil.llm import (
    CONSOLE_SYSTEM_PROMPT,
    EDGE_MAP_SUFFIX,
    LlmConfig,
    LlmError,
)


class CliPipelineTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        # The one-shot pipeline always touches the core (blank/crop/filter), so
        # skip the whole suite if the shared library is unavailable.
        try:
            from pystencil.core import get_core

            get_core()
        except Exception as e:  # pragma: no cover - environment-dependent
            raise unittest.SkipTest("native core unavailable: %s" % e)

    def setUp(self) -> None:
        self._dir = tempfile.TemporaryDirectory()
        self.tmp = self._dir.name

    def tearDown(self) -> None:
        self._dir.cleanup()

    def _path(self, name: str) -> str:
        """Absolute path inside this test's temp directory."""
        return os.path.join(self.tmp, name)

    def _run(self, args: list) -> str:
        """Invoke cli.main(args), asserting success, and return captured stderr."""
        err = io.StringIO()
        with contextlib.redirect_stderr(err):
            code = cli.main(args)
        self.assertEqual(code, 0, "cli.main exited non-zero; stderr=%r" % err.getvalue())
        return err.getvalue()

    def test_blank_writes_decodable_png(self) -> None:
        # `--blank 40 30 white out.png` writes a PNG decodable at 40x30.
        out = self._path("out.png")
        stderr = self._run(["--blank", "40", "30", "white", out])
        self.assertIn("wrote %s (40x30)" % out, stderr)
        with open(out, "rb") as fh:
            raw = fh.read()
        self.assertEqual(codecs.sniff(raw), "png")
        w, h, _ = codecs.decode(raw)
        self.assertEqual((w, h), (40, 30))

    def test_blank_with_bw_filter(self) -> None:
        # `--blank 40 30 --filter bw out.png` runs the filter and writes the image.
        out = self._path("bw.png")
        self._run(["--blank", "40", "30", "--filter", "bw", out])
        with open(out, "rb") as fh:
            w, h, _ = codecs.decode(fh.read())
        self.assertEqual((w, h), (40, 30))

    def test_save_layout_json_contains_image_width(self) -> None:
        # `--blank 40 30 --save-layout lay.json` writes a JSON layout file.
        lay = self._path("lay.json")
        self._run(["--blank", "40", "30", "--save-layout", lay])
        self.assertTrue(os.path.exists(lay))
        with open(lay, "r", encoding="utf-8") as fh:
            text = fh.read()
        self.assertIn("imageWidth", text)
        parsed = json.loads(text)
        self.assertEqual(parsed["imageWidth"], 40)
        self.assertEqual(parsed["imageHeight"], 30)

    def test_save_layout_dot_json_passthrough(self) -> None:
        # A path ending in .json is written verbatim (path semantics, /layout parity).
        lay = self._path("exact.json")
        self._run(["--blank", "20", "20", "--save-layout", lay])
        self.assertTrue(os.path.exists(lay))

    def test_save_layout_directory_prefix(self) -> None:
        # A non-.json path is a directory/prefix → "<dir>/<project>.json"; the
        # project name for a blank source is "blank".
        subdir = self._path("layouts")
        os.makedirs(subdir, exist_ok=True)
        self._run(["--blank", "20", "20", "--save-layout", subdir])
        expected = os.path.join(subdir, "blank.json")
        self.assertTrue(os.path.exists(expected), "expected %s" % expected)
        with open(expected, "r", encoding="utf-8") as fh:
            self.assertIn("imageWidth", fh.read())

    def test_no_source_is_an_error(self) -> None:
        # With neither --input nor --blank the pipeline reports an error.
        err = io.StringIO()
        with contextlib.redirect_stderr(err):
            code = cli.main([self._path("nope.png")])
        self.assertNotEqual(code, 0)
        self.assertIn("error:", err.getvalue())

    def test_consume_blank_leading_format_token(self) -> None:
        # `[format] [w h] [color]`: a case-insensitive named format leads; a colour
        # and a leftover output path still parse behind it.
        page, width, height, color, leftover = cli._consume_blank(["b5", "pink", "out.png"])
        self.assertEqual(page, "B5")
        self.assertIsNone(width)
        self.assertIsNone(height)
        self.assertEqual(color, "pink")
        self.assertEqual(leftover, ["out.png"])

    def test_consume_blank_dims_still_parse(self) -> None:
        # The pre-format grammar is unchanged: `w h [color]`.
        page, width, height, color, leftover = cli._consume_blank(["800", "600", "white"])
        self.assertIsNone(page)
        self.assertEqual((width, height), (800, 600))
        self.assertEqual(color, "white")
        self.assertEqual(leftover, [])

    def test_consume_blank_format_and_dims_are_exclusive(self) -> None:
        # PINNED: a format token and an explicit w h pair cannot be combined.
        with self.assertRaises(ValueError):
            cli._consume_blank(["a5", "800", "600"])

    def test_blank_named_format_writes_that_page(self) -> None:
        # `--blank b5 out.png` sizes the page from the B5 table entry (17.6×25cm @96dpi).
        from pystencil.core import get_core

        out = self._path("b5.png")
        stderr = self._run(["--blank", "b5", out])
        w, h = get_core().default_blank_size_px(17.6, 25.0)
        self.assertIn("wrote %s (%dx%d)" % (out, w, h), stderr)

    def test_repl_format_lists_sets_and_drives_blank(self) -> None:
        # /format bare lists the formats; /format b5 sets the session format, which
        # then drives the /blank default page.
        from pystencil.core import get_core

        out = io.StringIO()
        cli._Repl(out).run(io.StringIO("/format\n/format b5\n/blank\n"))
        text = out.getvalue()
        self.assertIn("A0", text)
        self.assertIn("C10", text)
        self.assertIn("custom <w> <h>", text)
        self.assertIn("page format B5 (17.6×25cm)", text)
        w, h = get_core().default_blank_size_px(17.6, 25.0)
        self.assertIn("blank %dx%d (white)" % (w, h), text)

    def test_repl_format_unknown_name_hints(self) -> None:
        out = io.StringIO()
        cli._Repl(out).run(io.StringIO("/format z9\n"))
        self.assertIn("type '/format' to list formats", out.getvalue())

    def test_repl_blank_format_token_adopts_session_format(self) -> None:
        # '/blank b5' makes B5 the session page format (mirror of the Zig console's
        # doBlank -> session.setPageSize): the next bare /blank is B5 again and the
        # exported layout carries pageSize "B5".
        from pystencil.core import get_core

        lay = self._path("blank-b5.json")
        out = io.StringIO()
        cli._Repl(out).run(io.StringIO("/blank b5\n/blank\n/layout %s\n" % lay))
        text = out.getvalue()
        w, h = get_core().default_blank_size_px(17.6, 25.0)
        self.assertEqual(text.count("blank %dx%d (white)" % (w, h)), 2)
        with open(lay, "r", encoding="utf-8") as f:
            self.assertEqual(json.load(f)["pageSize"], "B5")

    def test_repl_blank_explicit_dims_keep_session_format(self) -> None:
        # PINNED (Zig console "blank with explicit dims keeps the /format pick"):
        # a dims-only blank sizes the page but preserves the picked format, so the
        # exported layout still carries pageSize "B5".
        lay = self._path("blank-dims.json")
        out = io.StringIO()
        repl = cli._Repl(out)
        repl.run(io.StringIO("/format b5\n/blank 40 30\n/layout %s\n" % lay))
        self.assertEqual(repl._editor.page_format, "B5")
        with open(lay, "r", encoding="utf-8") as f:
            self.assertEqual(json.load(f)["pageSize"], "B5")

    def test_repl_blank_explicit_dims_keep_custom_pick(self) -> None:
        # Same for a custom pick: the format and its cm dims survive an explicit-dims
        # blank (mirror of the Zig console test's '/format custom 10 15' + '/blank 64 48').
        out = io.StringIO()
        repl = cli._Repl(out)
        repl.run(io.StringIO("/format custom 10 15\n/blank 64 48 red\n"))
        self.assertEqual(repl._editor.page_format, "custom")
        self.assertEqual(repl._editor.custom_page_width, 10.0)
        self.assertEqual(repl._editor.custom_page_height, 15.0)

    def test_repl_blank_explicit_dims_without_pick_stay_unset(self) -> None:
        # With no prior /format pick a dims-only blank leaves the format unset, so the
        # exported layout omits pageSize entirely.
        lay = self._path("blank-dims-unset.json")
        out = io.StringIO()
        cli._Repl(out).run(io.StringIO("/blank 40 30\n/layout %s\n" % lay))
        with open(lay, "r", encoding="utf-8") as f:
            self.assertNotIn("pageSize", json.load(f))

    def test_repl_bare_blank_uses_custom_pick_dims(self) -> None:
        # A bare /blank on a custom pick renders the custom cm dims at the default DPI
        # and keeps the pick (Zig console: 10×15cm @96dpi -> 378x567 px).
        from pystencil.core import get_core

        out = io.StringIO()
        repl = cli._Repl(out)
        repl.run(io.StringIO("/format custom 10 15\n/blank\n"))
        w, h = get_core().default_blank_size_px(10.0, 15.0)
        self.assertIn("blank %dx%d (white)" % (w, h), out.getvalue())
        self.assertEqual(repl._editor.page_format, "custom")
        self.assertEqual(repl._editor.custom_page_width, 10.0)
        self.assertEqual(repl._editor.custom_page_height, 15.0)

    def test_repl_bare_blank_custom_without_dims_falls_back_to_a4(self) -> None:
        # A layout can adopt pageSize "custom" with no (or only one) cm dimension
        # (adoptLayoutMeta keeps it raw); a bare /blank must fall through to the
        # default A4 blank like the Zig console — not error or blank a 1px page.
        from pystencil.core import get_core

        a4_w, a4_h = get_core().default_blank_size_px(21.0, 29.7)
        for meta in ({"pageSize": "custom"}, {"pageSize": "custom", "customPageWidth": 10.0}):
            out = io.StringIO()
            repl = cli._Repl(out)
            repl._editor.blank(8, 8)
            repl._editor.apply_layout(meta)
            repl.run(io.StringIO("/blank\n"))
            text = out.getvalue()
            self.assertNotIn("error", text, meta)
            self.assertIn("blank %dx%d (white)" % (a4_w, a4_h), text, meta)
            # The unusable pick does not survive the blank (nothing to restore).
            self.assertEqual(repl._editor.page_format, "", meta)

    def test_repl_bare_blank_with_unknown_adopted_format_falls_back_to_a4(self) -> None:
        # A layout can carry an unknown pageSize (adopted raw, like adoptLayoutMeta);
        # a bare /blank then quietly creates the default A4 blank instead of erroring
        # (the Zig console maps it through canonicalPageFormat -> null).
        from pystencil.core import get_core

        out = io.StringIO()
        repl = cli._Repl(out)
        repl._editor.blank(8, 8)
        repl._editor.apply_layout({"pageSize": "Z9"})
        repl.run(io.StringIO("/blank\n"))
        text = out.getvalue()
        self.assertNotIn("error", text)
        w, h = get_core().default_blank_size_px(21.0, 29.7)
        self.assertIn("blank %dx%d (white)" % (w, h), text)

    def test_repl_format_custom_rejects_nan_and_out_of_range(self) -> None:
        # NaN/inf and out-of-range cm dims are rejected (parseCmDim's 0.1–500 pin) so
        # the exported layout can never contain a non-RFC-8259 `NaN` constant.
        for spec in ("nan nan", "inf 10", "1000 1000", "0.05 10"):
            out = io.StringIO()
            repl = cli._Repl(out)
            repl.run(io.StringIO("/format custom %s\n" % spec))
            text = out.getvalue()
            self.assertIn("error: custom takes width + height in cm (0.1-500)", text)
            self.assertEqual(repl._editor.page_format, "", spec)

    def test_repl_bare_filter_lists_variants(self) -> None:
        # A bare /filter lists the possible modes instead of erroring out.
        out = io.StringIO()
        cli._Repl(out).run(io.StringIO("/filter\n"))
        text = out.getvalue()
        self.assertNotIn("error:", text)
        for variant in ("bw", "sepia", "invert", "contour", "none"):
            self.assertIn(variant, text)


class _MockLlmClient:
    """A canned LlmClient stand-in recording the messages /prompt sends (offline)."""

    def __init__(self, reply: str = "", raises: Exception = None, replies: list = None) -> None:
        self.reply = reply
        self.replies = list(replies or [])
        self.raises = raises
        self.sent: list = []
        self.systems: list = []

    def chat(self, messages, system=None):
        self.sent.append([dict(m) for m in messages])
        self.systems.append(system)
        if self.raises is not None:
            raise self.raises
        return self.replies.pop(0) if self.replies else self.reply


class ReplLlmConfigTest(unittest.TestCase):
    """The /llm command: config display (masked) and in-session overrides.

    These never build a client or touch the native core, so they run everywhere.
    """

    def _repl(self, **cfg_kw):
        out = io.StringIO()
        repl = cli._Repl(out)
        # Pin the session config so the host's STENCIL_LLM_* env can't leak in.
        repl._llm = LlmConfig(**cfg_kw)
        return repl, out

    def test_bare_llm_masks_credentials(self) -> None:
        repl, out = self._repl(provider="openai-compat", api_key="sk-secret1234")
        repl.run(io.StringIO("/llm\n"))
        text = out.getvalue()
        self.assertIn("llm provider openai-compat", text)
        self.assertIn("http://localhost:1234/v1", text)
        self.assertNotIn("sk-secret1234", text)  # never echo the key
        self.assertIn("****1234", text)  # masked to stars + last 4
        self.assertIn("model  (default)", text)

    def test_provider_switch_refills_default_url(self) -> None:
        repl, out = self._repl()  # ollama, default URL
        repl.run(io.StringIO("/llm provider openai-compat\n"))
        self.assertEqual(repl._llm.provider, "openai-compat")
        self.assertEqual(repl._llm.base_url, "http://localhost:1234/v1")
        repl.run(io.StringIO("/llm provider ollama\n"))
        self.assertEqual(repl._llm.base_url, "http://localhost:11434")

    def test_user_url_survives_provider_switch(self) -> None:
        repl, out = self._repl()
        repl.run(io.StringIO("/llm url http://box:9999/v1\n/llm provider openai-compat\n"))
        self.assertEqual(repl._llm.provider, "openai-compat")
        # The session override wins over the provider's default re-fill.
        self.assertEqual(repl._llm.base_url, "http://box:9999/v1")

    def test_unknown_provider_is_rejected(self) -> None:
        repl, out = self._repl()
        repl.run(io.StringIO("/llm provider anthropic-direct\n"))
        self.assertIn("error: unknown provider 'anthropic-direct'", out.getvalue())
        self.assertEqual(repl._llm.provider, "ollama")  # unchanged

    def test_model_key_server_setters(self) -> None:
        repl, out = self._repl()
        repl.run(
            io.StringIO(
                "/llm model llava\n/llm key sk-1\n/llm server https://srv:8090\n"
                "/llm model\n/llm key\n"
            )
        )
        self.assertEqual(repl._llm.server_url, "https://srv:8090")
        self.assertEqual(repl._llm.model, "")  # bare value clears back to default
        self.assertEqual(repl._llm.api_key, "")
        text = out.getvalue()
        self.assertIn("llm model llava", text)
        self.assertIn("llm key set", text)
        self.assertIn("llm key cleared", text)
        self.assertNotIn("sk-1", text)  # the key value is never echoed

    def test_unknown_subcommand_hints(self) -> None:
        repl, out = self._repl()
        repl.run(io.StringIO("/llm temperature 0.7\n"))
        self.assertIn("error: /llm takes provider | url | model | key | server",
                      out.getvalue())

    def test_help_lists_llm_commands(self) -> None:
        out = io.StringIO()
        cli._Repl(out).run(io.StringIO("/help\n"))
        text = out.getvalue()
        self.assertIn("/prompt <text>", text)
        self.assertIn("/llm [key value]", text)

    def test_stencil_server_reuses_live_connection_token(self) -> None:
        # A /connect-ed server whose URL matches (or the first one, when no server
        # URL is configured) lends its bearer token to the LLM client.
        from pystencil.server import ServerConnection

        repl, out = self._repl(provider="stencil-server")
        conn = ServerConnection("http://host:8090", token="sess-tok")
        repl._manager._conns[conn.base] = conn  # seed without a network handshake
        client = repl._llm_client()
        req = client._build_request([{"role": "user", "text": "hi"}])
        self.assertEqual(req.full_url, "http://host:8090/llm/chat")
        self.assertEqual(req.get_header("Authorization"), "Bearer sess-tok")
        # An explicit non-matching server URL does NOT borrow the token.
        repl._llm.server_url = "https://other:8090"
        req = repl._llm_client()._build_request([{"role": "user", "text": "hi"}])
        self.assertEqual(req.full_url, "https://other:8090/llm/chat")
        self.assertEqual(req.get_header("Authorization"), "Bearer ")


class ReplConnectTokenTest(unittest.TestCase):
    """/connect's `token=` argument — the only way onto an ADMIN_TOKEN-gated server."""

    def _repl(self) -> tuple:
        out = io.StringIO()
        repl = cli._Repl(out)
        seen: list = []
        # Stand in for the network handshake; record what the manager was handed.
        repl._manager.connect = lambda spec: seen.append(spec) or repl._manager
        return repl, out, seen

    def test_token_argument_is_passed_to_the_manager(self) -> None:
        repl, out, seen = self._repl()
        repl.run(io.StringIO("/connect http://host:8090 token=sess-tok\n"))
        self.assertEqual(seen, [{"url": "http://host:8090", "token": "sess-tok"}])
        self.assertIn("connected http://host:8090", out.getvalue())

    def test_token_applies_to_every_url_in_the_call(self) -> None:
        repl, _out, seen = self._repl()
        repl.run(io.StringIO("/connect http://a:8090 http://b:8090 token=t\n"))
        self.assertEqual(seen, [{"url": "http://a:8090", "token": "t"},
                                {"url": "http://b:8090", "token": "t"}])

    def test_without_a_token_the_url_is_passed_as_before(self) -> None:
        repl, _out, seen = self._repl()
        repl.run(io.StringIO("/connect http://host:8090\n"))
        self.assertEqual(seen, ["http://host:8090"])

    def test_a_token_alone_is_not_a_url(self) -> None:
        repl, out, seen = self._repl()
        repl.run(io.StringIO("/connect token=t\n"))
        self.assertEqual(seen, [])
        self.assertIn("error: /connect needs one or more server URLs", out.getvalue())


class ReplPromptOfflineTest(unittest.TestCase):
    """/prompt paths that need no native core (no image loaded, mock client)."""

    def _repl(self, client) -> tuple:
        out = io.StringIO()
        repl = cli._Repl(out)
        repl._llm = LlmConfig()
        repl._llm_client = lambda: client  # inject the offline mock
        return repl, out

    def test_prompt_requires_text(self) -> None:
        repl, out = self._repl(_MockLlmClient("unused"))
        repl.run(io.StringIO("/prompt\n"))
        self.assertIn("error: /prompt needs text to send", out.getvalue())

    def test_chat_only_prompt_prints_reply(self) -> None:
        client = _MockLlmClient("Just words, no plan.")
        repl, out = self._repl(client)
        repl.run(io.StringIO("/prompt what can you do?\n"))
        self.assertIn("Just words, no plan.", out.getvalue())
        # No image loaded => a text-only turn (no attachment).
        self.assertEqual(client.sent[0][0]["text"], "what can you do?")
        self.assertEqual(client.sent[0][0]["images"], [])

    def test_stop_reason_error_prints_as_console_error(self) -> None:
        client = _MockLlmClient(
            raises=LlmError("response truncated (max_tokens) — not parsed as a plan",
                            stop_reason="max_tokens")
        )
        repl, out = self._repl(client)
        repl.run(io.StringIO("/prompt do things\n/status\n"))
        text = out.getvalue()
        self.assertIn("error: response truncated (max_tokens)", text)
        self.assertIn("no image loaded", text)  # the REPL kept running

    def test_invalid_plan_prints_as_console_error(self) -> None:
        client = _MockLlmClient(
            '{"version":1,"reply":"bad","actions":[{"op":"rotate","dir":"up"}]}'
        )
        repl, out = self._repl(client)
        repl.run(io.StringIO("/p spin it\n"))  # the /p alias routes here too
        self.assertIn('error: invalid "rotate" action', out.getvalue())


class ReplChatModeTest(unittest.TestCase):
    """/chat (§12): the on/off toggle, multi-turn /prompt routing, and clear.

    Offline like ReplPromptOfflineTest — no image loaded, injected mock client.
    """

    def _repl(self, client) -> tuple:
        out = io.StringIO()
        repl = cli._Repl(out)
        repl._llm = LlmConfig()
        repl._llm_client = lambda: client  # inject the offline mock
        return repl, out

    def test_chat_toggles_and_show(self) -> None:
        repl, out = self._repl(_MockLlmClient("words"))
        repl.run(io.StringIO("/chat\n/chat on\n/chat show\n/chat off\n/chat\n"))
        text = out.getvalue()
        # Default OFF, bare /chat and /chat show print mode + turn count.
        self.assertIn("chat off (0 message(s))", text.splitlines()[0])
        self.assertIn("chat on", text)
        self.assertIn("chat on (0 message(s))", text)
        self.assertEqual(text.splitlines()[-1], "chat off (0 message(s))")

    def test_chat_on_says_who_can_read_a_saved_chat(self) -> None:
        # §12.2: a transcript records what the user asked for in their own words, and on
        # a server project it carries the PROJECT's access — which is not what "save
        # chats with the project" sounds like it promises. So the console has to say it
        # at the toggle, on the turn that enables saving, before anything is written.
        repl, out = self._repl(_MockLlmClient("words"))
        repl.run(io.StringIO("/chat on\n"))
        after_toggle = out.getvalue().split("chat on", 1)[1]
        self.assertIn("shared with", after_toggle)
        self.assertIn("readable by everyone", after_toggle)
        # Both destinations named: the local .stencil file and the server project.
        self.assertIn(".stencil project", after_toggle)
        self.assertIn("server project", after_toggle)

    def test_chat_unknown_subcommand_hints(self) -> None:
        repl, out = self._repl(_MockLlmClient("words"))
        repl.run(io.StringIO("/chat maybe\n"))
        self.assertIn("error: /chat takes on | off | clear | show", out.getvalue())
        self.assertFalse(repl._chat_on)  # unchanged

    def test_chat_on_accumulates_turns_across_prompts(self) -> None:
        client = _MockLlmClient("Just words, no plan.")
        repl, out = self._repl(client)
        repl.run(io.StringIO("/chat on\n/prompt first\n/prompt second\n/chat\n"))
        # Turn two replayed the whole bounded history: user, assistant, user.
        self.assertEqual(len(client.sent[0]), 1)
        self.assertEqual(
            [m["role"] for m in client.sent[1]], ["user", "assistant", "user"]
        )
        self.assertEqual(client.sent[1][0]["text"], "first")
        self.assertEqual(client.sent[1][2]["text"], "second")
        self.assertEqual(len(repl._chat.history), 4)
        self.assertIn("chat on (4 message(s))", out.getvalue())

    def test_chat_clear_empties_the_conversation(self) -> None:
        client = _MockLlmClient("Just words, no plan.")
        repl, out = self._repl(client)
        repl.run(io.StringIO("/chat on\n/prompt first\n/chat clear\n/chat\n"))
        self.assertIn("chat cleared", out.getvalue())
        self.assertEqual(repl._chat.history, [])
        self.assertIn("chat on (0 message(s))", out.getvalue())

    def test_default_off_keeps_prompts_single_turn(self) -> None:
        # With the toggle off (the default), /prompt stays exactly single-turn:
        # each request carries exactly one user message, nothing accumulates.
        client = _MockLlmClient("Just words, no plan.")
        repl, out = self._repl(client)
        repl.run(io.StringIO("/prompt first\n/prompt second\n"))
        self.assertEqual(len(client.sent), 2)
        for sent in client.sent:
            self.assertEqual(len(sent), 1)
            self.assertEqual(sent[0]["role"], "user")
        self.assertIsNone(repl._chat)

    class _RecordingConn:
        """The one ServerConnection method /chat clear touches, recorded."""

        def __init__(self) -> None:
            self.deleted = []

        def delete_file(self, pid, kind) -> None:
            self.deleted.append((pid, kind))

    def test_chat_clear_deletes_the_active_remote_chat(self) -> None:
        # While a fetched project is active and /chat is on, clear also drops
        # the server-side `chat` file (§12).
        conn = self._RecordingConn()
        repl, out = self._repl(_MockLlmClient("words"))
        repl._remote = (conn, "p1")  # as recorded by /fetch
        repl.run(io.StringIO("/chat on\n/chat clear\n"))
        self.assertEqual(conn.deleted, [("p1", "chat")])
        self.assertIn("chat cleared", out.getvalue())

    def test_image_swap_detaches_the_previous_remote_project(self) -> None:
        # Replacing the working image (/drop here) must reset the remote scope
        # and the conversation, so a later /chat clear can never delete the
        # PREVIOUS project's server-side chat file.
        conn = self._RecordingConn()
        repl, out = self._repl(_MockLlmClient("words"))
        repl._remote = (conn, "p1")  # as recorded by /fetch
        repl.run(io.StringIO("/chat on\n/prompt hello\n/drop\n/chat clear\n"))
        self.assertIsNone(repl._remote)
        self.assertIsNone(repl._chat)  # the conversation is image-scoped too
        self.assertEqual(conn.deleted, [])
        self.assertIn("chat cleared", out.getvalue())

    def test_help_lists_chat_command(self) -> None:
        out = io.StringIO()
        cli._Repl(out).run(io.StringIO("/help\n"))
        self.assertIn("/chat [on|off|clear]", out.getvalue())


class ReplClearChatOpTest(unittest.TestCase):
    """§10 clearChat: deferred to the end of the turn, confirmed with a y/N line
    on the console's own input, and on a yes cleared through the exact /chat
    clear path. Offline like ReplChatModeTest — no image, injected mock client."""

    PLAN = '{"version":1,"reply":"Wiping.","actions":[{"op":"clearChat"}]}'

    def _repl(self, client) -> tuple:
        out = io.StringIO()
        repl = cli._Repl(out)
        repl._llm = LlmConfig()
        repl._llm_client = lambda: client  # inject the offline mock
        return repl, out

    def test_confirm_is_deferred_past_the_plans_other_actions(self) -> None:
        repl, out = self._repl(_MockLlmClient(self.PLAN))
        repl.run(io.StringIO("/prompt wipe the chat\nn\n"))
        text = out.getvalue()
        # Reply, then the executed actions, THEN the confirm — never mid-plan.
        self.assertLess(text.index("Wiping."), text.index("applied 1 action(s)"))
        self.assertLess(text.index("applied 1 action(s)"),
                        text.index("clear this conversation's history? [y/N]"))
        self.assertIn("clear canceled", text)

    def test_declined_confirm_is_a_note_that_keeps_the_conversation(self) -> None:
        client = _MockLlmClient(replies=["just words", self.PLAN])
        repl, out = self._repl(client)
        repl.run(io.StringIO("/chat on\n/prompt hello\n/prompt wipe it\nn\n/chat\n"))
        text = out.getvalue()
        self.assertIn("clear canceled", text)
        self.assertNotIn("chat cleared", text)
        self.assertEqual(len(repl._chat.history), 4)  # both turns survived
        self.assertIn("chat on (4 message(s))", text)

    def test_accepted_confirm_runs_the_chat_clear_path(self) -> None:
        # The confirmed clear IS /chat clear: history emptied and the active
        # remote project's server-side `chat` file dropped (§12).
        conn = ReplChatModeTest._RecordingConn()
        client = _MockLlmClient(replies=["just words", self.PLAN])
        repl, out = self._repl(client)
        repl._remote = (conn, "p1")  # as recorded by /fetch
        repl.run(io.StringIO("/chat on\n/prompt hello\n/prompt wipe it\ny\n/chat\n"))
        text = out.getvalue()
        self.assertIn("chat cleared", text)
        self.assertEqual(repl._chat.history, [])
        self.assertEqual(conn.deleted, [("p1", "chat")])
        self.assertIn("chat on (0 message(s))", text)

    def test_eof_on_the_confirm_declines(self) -> None:
        repl, out = self._repl(_MockLlmClient(self.PLAN))
        repl.run(io.StringIO("/prompt wipe it\n"))  # stream ends before an answer
        self.assertIn("clear canceled", out.getvalue())
        self.assertNotIn("chat cleared", out.getvalue())


class ReplPromptNativeTest(unittest.TestCase):
    """/prompt round-trips executing a real plan on the session's editor."""

    @classmethod
    def setUpClass(cls) -> None:
        try:
            from pystencil.core import get_core

            get_core()
        except Exception as e:  # pragma: no cover - environment-dependent
            raise unittest.SkipTest("native core unavailable: %s" % e)

    def setUp(self) -> None:
        self._dir = tempfile.TemporaryDirectory()
        self._old_cwd = os.getcwd()
        os.chdir(self._dir.name)  # /prompt writes variant files into the cwd

    def tearDown(self) -> None:
        os.chdir(self._old_cwd)
        self._dir.cleanup()

    def test_prompt_executes_plan_and_writes_variants(self) -> None:
        plan = (
            '{"version":1,"reply":"rotated; one variant",'
            '"actions":[{"op":"rotate","dir":"right"}],'
            '"variants":[{"label":"B&W One!","actions":[{"op":"filter","mode":"bw"}]}]}'
        )
        client = _MockLlmClient(plan)
        out = io.StringIO()
        repl = cli._Repl(out)
        repl._llm = LlmConfig()
        repl._llm_client = lambda: client
        repl.run(io.StringIO("/blank 32 48\n/prompt rotate it and add a b&w variant\n"))
        text = out.getvalue()
        self.assertIn("rotated; one variant", text)
        self.assertIn("applied 1 action(s) -> 48x32", text)
        # The current image rode along base64-able as a PNG attachment.
        media_type, data = client.sent[0][0]["images"][0]
        self.assertEqual(media_type, "image/png")
        self.assertEqual(codecs.decode(bytes(data))[:2], (32, 48))
        # The session editor was mutated in place by the top-level actions.
        self.assertEqual(repl._editor.image_size, (48, 32))
        # The variant landed as variant-<sanitized-label>.png with the wrote line.
        self.assertIn("wrote variant-b-w-one.png (48x32)", text)
        with open("variant-b-w-one.png", "rb") as fh:
            w, h, _pixels = codecs.decode(fh.read())
        self.assertEqual((w, h), (48, 32))

    def test_misplaced_variant_op_drops_the_variant_not_the_turn(self) -> None:
        # §1: a `clear` inside a variant costs THAT variant only — the top-level
        # actions and the well-formed variants still run, with a warning.
        plan = (
            '{"version":1,"reply":"rotated; two variants",'
            '"actions":[{"op":"rotate","dir":"right"}],'
            '"variants":[{"label":"wiped","actions":[{"op":"clear"}]},'
            '{"label":"grey","actions":[{"op":"filter","mode":"bw"}]}]}'
        )
        out = io.StringIO()
        repl = cli._Repl(out)
        repl._llm = LlmConfig()
        repl._llm_client = lambda: _MockLlmClient(plan)
        repl.run(io.StringIO("/blank 32 48\n/prompt rotate it, wipe it, grey it\n"))
        text = out.getvalue()
        self.assertNotIn("error:", text)
        self.assertIn('[warning] dropped variant 1 ("wiped") — the "clear" op adjusts '
                      "the console, not the image, and cannot appear in a variant", text)
        self.assertIn("applied 1 action(s) -> 48x32", text)
        self.assertIn("wrote variant-grey.png (48x32)", text)
        self.assertTrue(repl._editor.has_image())  # the image survived

    def test_a_plan_of_only_a_bad_variant_is_a_reply_plus_warning(self) -> None:
        plan = (
            '{"version":1,"reply":"here you go","actions":[],'
            '"variants":[{"label":"wiped","actions":[{"op":"clear"}]}]}'
        )
        out = io.StringIO()
        repl = cli._Repl(out)
        repl._llm = LlmConfig()
        repl._llm_client = lambda: _MockLlmClient(plan)
        repl.run(io.StringIO("/blank 32 48\n/prompt wipe it in a variant\n"))
        text = out.getvalue()
        self.assertNotIn("error:", text)
        self.assertIn("here you go", text)
        self.assertIn('[warning] dropped variant 1 ("wiped")', text)
        self.assertTrue(repl._editor.has_image())

    def test_prompt_reuses_encoded_png_until_the_image_changes(self) -> None:
        # Contract §7 payload control: an unchanged working image is not re-encoded
        # on the next /prompt turn; any edit (new history state) re-encodes.
        client = _MockLlmClient("chat only, no plan")
        out = io.StringIO()
        repl = cli._Repl(out)
        repl._llm = LlmConfig()
        repl._llm_client = lambda: client
        repl.run(io.StringIO("/blank 16 24\n/prompt a\n/prompt b\n/rotate 1\n/prompt c\n"))
        first = client.sent[0][0]["images"][0][1]
        second = client.sent[1][0]["images"][0][1]
        third = client.sent[2][0]["images"][0][1]
        self.assertIs(second, first)  # same edit state => the same encode object
        self.assertIsNot(third, first)  # the rotate invalidated the memo
        self.assertEqual(codecs.decode(bytes(third))[:2], (24, 16))

    def test_prompt_skips_oversized_image_with_note(self) -> None:
        client = _MockLlmClient("noted")
        out = io.StringIO()
        repl = cli._Repl(out)
        repl._llm = LlmConfig()
        repl._llm_client = lambda: client
        original = cli._PROMPT_IMAGE_LIMIT
        cli._PROMPT_IMAGE_LIMIT = 16  # force the "too large" branch cheaply
        try:
            repl.run(io.StringIO("/blank 32 32\n/prompt describe this\n"))
        finally:
            cli._PROMPT_IMAGE_LIMIT = original
        text = out.getvalue()
        self.assertIn("sending text only", text)
        self.assertEqual(client.sent[0][0]["images"], [])  # attachment skipped
        # Snapshot dropped ⇒ edge map dropped too, so no edge-map sentence (the
        # console context always rides the spliced console prompt).
        self.assertTrue(client.systems[0].startswith(CONSOLE_SYSTEM_PROMPT))
        self.assertNotIn(EDGE_MAP_SUFFIX, client.systems[0])

    def _repl(self, client) -> tuple:
        out = io.StringIO()
        repl = cli._Repl(out)
        repl._llm = LlmConfig()
        repl._llm_client = lambda: client
        return repl, out

    def test_prompt_attaches_edge_map_with_suffix(self) -> None:
        client = _MockLlmClient("just words")
        repl, _out = self._repl(client)
        repl.run(io.StringIO("/blank 32 48\n/prompt outline the box\n"))
        images = client.sent[0][0]["images"]
        self.assertEqual(len(images), 2)  # snapshot, then the edge map
        media_type, edge = images[1]
        self.assertEqual(media_type, "image/png")
        self.assertEqual(codecs.decode(bytes(edge))[:2], (32, 48))  # same pixel frame
        # The sentence rides the system prompt exactly when the edge map is attached,
        # after the console-context suffix on the spliced console prompt.
        self.assertTrue(client.systems[0].startswith(CONSOLE_SYSTEM_PROMPT))
        self.assertTrue(client.systems[0].endswith("\n\n" + EDGE_MAP_SUFFIX))

    def test_prompt_remaps_layout_through_plan_crop(self) -> None:
        # Contract §1: /prompt plans arrive in the pre-plan frame; the executor
        # re-maps the layout through the plan's own crop before drawing.
        plan = (
            '{"version":1,"reply":"cropped and lined",'
            '"actions":[{"op":"crop","spec":{"x1":"100px"}},'
            '{"op":"layout","lines":[{"points":[{"x":150,"y":50},{"x":160,"y":60}]}]}]}'
        )
        client = _MockLlmClient(replies=[plan])
        repl, _out = self._repl(client)
        repl.run(io.StringIO("/blank 200 100\n/prompt crop then draw\n"))
        self.assertEqual(repl._editor.image_size, (100, 100))
        points = [(p.x, p.y) for p in repl._editor.layout().lines[-1].points]
        self.assertEqual(points, [(50.0, 50.0), (60.0, 60.0)])

    def test_multi_image_plan_saves_a_project(self) -> None:
        # §2.1: `save` writes <name>.stencil beside the output. The console attaches
        # no images of its own, so the switch itself is skipped with a per-action
        # note — the rest of the plan still runs, in the turn's ONE model round.
        plan = (
            '{"version":1,"reply":"kept it",'
            '"actions":[{"op":"image","index":1},'
            '{"op":"layout","lines":[{"points":[{"x":1,"y":2},{"x":3,"y":4}]}]},'
            '{"op":"save","name":"kept"}]}'
        )
        client = _MockLlmClient(plan)
        repl, out = self._repl(client)
        repl.run(io.StringIO("/blank 32 48\n/prompt outline and keep it\n"))
        text = out.getvalue()
        self.assertEqual(len(client.sent), 1)
        self.assertNotIn("correction", text)
        self.assertIn("[warning] Skipped switching to attached image 1", text)
        self.assertIn("saved project kept.stencil", text)
        reopened = Editor().open_project("kept.stencil")
        self.assertEqual(reopened.image_size, (32, 48))

    def test_oversized_edge_map_alone_is_dropped(self) -> None:
        client = _MockLlmClient("noted")
        repl, _out = self._repl(client)
        repl._edge_map_png = lambda: b"e" * (cli._PROMPT_IMAGE_LIMIT + 1)
        repl.run(io.StringIO("/blank 16 16\n/prompt hi\n"))
        images = client.sent[0][0]["images"]
        self.assertEqual(len(images), 1)  # the snapshot still rides along
        self.assertNotIn(EDGE_MAP_SUFFIX, client.systems[0])  # no edge sentence

    def test_chat_mode_never_replays_the_edge_map(self) -> None:
        client = _MockLlmClient("chat only")
        repl, _out = self._repl(client)
        repl.run(io.StringIO("/blank 16 24\n/chat on\n/prompt a\n/prompt b\n"))
        wire2 = client.sent[1]
        self.assertEqual(len(wire2[0]["images"]), 1)  # prior turn: snapshot only
        self.assertEqual(len(wire2[-1]["images"]), 2)  # current: snapshot + edge map

    _LAYOUT_PLAN = (
        '{"version":1,"reply":"outlined",'
        '"actions":[{"op":"layout","lines":[{"points":[{"x":1,"y":2},{"x":3,"y":4}],'
        '"color":"#123456"}]}]}'
    )

    def test_layout_plan_is_exactly_one_model_round(self) -> None:
        # §3.0: the plan executes, the reply is shown, the turn ends — the second
        # canned reply proves no further request goes out.
        client = _MockLlmClient(
            replies=[self._LAYOUT_PLAN, '{"version":1,"reply":"never sent"}']
        )
        repl, out = self._repl(client)
        repl.run(io.StringIO("/blank 32 48\n/prompt outline the box\n"))
        self.assertEqual(len(client.sent), 1)
        self.assertEqual(len(client.replies), 1)  # the extra reply stayed unsent
        text = out.getvalue()
        self.assertIn("outlined", text)
        self.assertNotIn("[warning]", text)
        # The traced line stands exactly as planned, styling kept.
        line = repl._editor.layout().lines[-1]
        self.assertEqual([(p.x, p.y) for p in line.points], [(1, 2), (3, 4)])
        self.assertEqual(line.color, "#123456")

    def test_a_layout_turn_prints_no_post_plan_note(self) -> None:
        client = _MockLlmClient(replies=[self._LAYOUT_PLAN])
        repl, out = self._repl(client)
        repl.run(io.StringIO("/blank 32 48\n/prompt outline the box\n"))
        text = out.getvalue().lower()
        for phrase in ("correction", "self-check", "checking the outlines", "sharpen"):
            self.assertNotIn(phrase, text)


if __name__ == "__main__":
    unittest.main()


class ContinuationPredicateTest(unittest.TestCase):
    """§7 (amended): a plan that loads and drew no layout continues; one that traced
    already committed to its coordinates and does not."""

    def _plan(self, ops):
        class P:
            actions = [{"op": o} for o in ops]
            variants = []
        return P()

    def test_a_mixed_load_plan_without_a_layout_continues(self):
        from pystencil.cli import _load_only_plan
        self.assertTrue(_load_only_plan(self._plan(["blank", "filter", "crop"])))

    def test_a_load_plan_that_drew_a_layout_does_not_continue(self):
        from pystencil.cli import _load_only_plan
        self.assertFalse(_load_only_plan(self._plan(["blank", "layout"])))

    def test_a_planless_or_editing_only_turn_does_not_continue(self):
        from pystencil.cli import _load_only_plan
        self.assertFalse(_load_only_plan(self._plan([])))
        self.assertFalse(_load_only_plan(self._plan(["filter"])))

    def test_an_open_url_load_plan_continues(self):
        from pystencil.cli import _load_only_plan
        self.assertTrue(_load_only_plan(self._plan(["openUrl", "filter"])))
        self.assertFalse(_load_only_plan(self._plan(["openUrl", "layout"])))


class _StubConn:
    """A stand-in live ServerConnection: a base URL, a token that must never leak
    into a prompt, and a canned project listing."""

    def __init__(self, base, projects=(), token="sekrit-token") -> None:
        self.base = base
        self.token = token
        self._projects = [
            {"name": n, "id": "p%d" % i} for i, n in enumerate(projects)
        ]
        self.closed = False

    def list_projects(self):
        return list(self._projects)

    def close(self):
        self.closed = True


def _wire_repl(client, urls=()):
    """A REPL with an offline LLM client and stub live connections installed."""
    out = io.StringIO()
    repl = cli._Repl(out)
    repl._llm = LlmConfig()
    repl._llm_client = lambda: client
    for u in urls:
        conn = u if isinstance(u, _StubConn) else _StubConn(u)
        repl._manager._conns[conn.base] = conn
    return repl, out


class ReplDisconnectCommandTest(unittest.TestCase):
    """The new /disconnect REPL command (Zig-console wording)."""

    def test_disconnect_without_connections(self) -> None:
        repl, out = _wire_repl(_MockLlmClient(""))
        repl.run(io.StringIO("/disconnect\n"))
        self.assertIn("no server connections", out.getvalue())

    def test_bare_disconnect_drops_the_most_recent(self) -> None:
        repl, out = _wire_repl(_MockLlmClient(""), ["http://a.example:8090", "http://b.example:8090"])
        repl.run(io.StringIO("/disconnect\n"))
        self.assertIn("disconnected from http://b.example:8090", out.getvalue())
        self.assertEqual(repl._manager.connections, ["http://a.example:8090"])

    def test_disconnect_by_url_and_unknown(self) -> None:
        repl, out = _wire_repl(_MockLlmClient(""), ["http://a.example:8090"])
        repl.run(io.StringIO("/disconnect http://b.example:9\n/disconnect http://a.example:8090\n"))
        text = out.getvalue()
        self.assertIn("not connected to http://b.example:9", text)
        self.assertIn("disconnected from http://a.example:8090", text)
        self.assertEqual(repl._manager.connections, [])

    def test_help_lists_disconnect_and_delete(self) -> None:
        out = io.StringIO()
        cli._Repl(out).run(io.StringIO("/help\n"))
        self.assertIn("/disconnect [url]", out.getvalue())
        self.assertIn("/delete <x.stencil>", out.getvalue())


class ReplDeleteCommandTest(unittest.TestCase):
    """The new /delete REPL command: the cli console's full guard set + wording."""

    def setUp(self) -> None:
        self._dir = tempfile.TemporaryDirectory()
        self._old_cwd = os.getcwd()
        os.chdir(self._dir.name)

    def tearDown(self) -> None:
        os.chdir(self._old_cwd)
        self._dir.cleanup()

    def _run(self, script: str) -> str:
        out = io.StringIO()
        cli._Repl(out).run(io.StringIO(script))
        return out.getvalue()

    def test_delete_removes_a_stencil_file(self) -> None:
        with open("p.stencil", "w", encoding="utf-8") as fh:
            fh.write("{}")
        text = self._run("/delete p.stencil\n")
        self.assertIn("deleted p.stencil", text)
        self.assertFalse(os.path.exists("p.stencil"))

    def test_delete_guards(self) -> None:
        text = self._run(
            "/delete\n"
            "/delete http://x.example/p.stencil\n"
            "/delete image.png\n"
            "/delete ../escape.stencil\n"
        )
        self.assertIn("error: delete needs a .stencil path", text)
        self.assertIn("error: delete only removes local files, not URLs", text)
        self.assertIn("error: delete only removes .stencil project files (got 'image.png')", text)
        self.assertIn(
            "error: refusing to delete a path that escapes the working directory: "
            "'../escape.stencil'",
            text,
        )

    def test_delete_missing_file_reports(self) -> None:
        self.assertIn("error: could not delete ghost.stencil", self._run("/delete ghost.stencil\n"))

    def test_rm_alias_routes_here(self) -> None:
        with open("q.stencil", "w", encoding="utf-8") as fh:
            fh.write("{}")
        self.assertIn("deleted q.stencil", self._run("/rm q.stencil\n"))

    def test_api_delete_project_gains_the_traversal_guard(self) -> None:
        # The cli's guard, now on the API too (contract §10 pystencil paragraph).
        with self.assertRaises(ValueError):
            Editor.delete_project("../escape.stencil")
        with self.assertRaises(ValueError):
            Editor.delete_project("sub/../../escape.stencil")
        with self.assertRaises(ValueError):
            Editor.delete_project("http://x.example/p.stencil")
        self.assertEqual(Editor.delete_reject("ok.stencil"), None)
        self.assertEqual(Editor.delete_reject("..\\win.stencil"), "traversal")


class ReplConsoleContextTest(unittest.TestCase):
    """The §4 console-context suffix /prompt sends: connections + active project +
    capped project names — and never a token."""

    def test_prompt_carries_the_console_state(self) -> None:
        client = _MockLlmClient("just words")
        repl, _out = _wire_repl(
            client,
            [_StubConn("http://a.example:8090", projects=["portrait", "cat 2"])],
        )
        repl.run(io.StringIO("/prompt what is on my server?\n"))
        system = client.systems[0]
        self.assertTrue(system.startswith(CONSOLE_SYSTEM_PROMPT))
        self.assertIn("Console state", system)
        self.assertIn("Connections (1): http://a.example:8090.", system)
        self.assertIn("Projects on http://a.example:8090: portrait, cat 2.", system)
        self.assertIn("Active server project: none.", system)
        self.assertNotIn("sekrit-token", system)
        self.assertNotIn("token", system.split("Console state", 1)[1].lower())

    def test_active_fetched_project_is_named(self) -> None:
        client = _MockLlmClient("noted")
        conn = _StubConn("http://a.example:8090", projects=["portrait"])
        repl, _out = _wire_repl(client, [conn])
        repl._remote = (conn, "p0")  # as /fetch records it
        repl._editor._name = "portrait"  # the fetched project's name
        repl.run(io.StringIO("/prompt which project am I on?\n"))
        system = client.systems[0]
        self.assertIn("http://a.example:8090 (active project's server)", system)
        self.assertIn('Active server project: "portrait".', system)

    def test_unreachable_server_omits_its_listing(self) -> None:
        client = _MockLlmClient("noted")
        conn = _StubConn("http://a.example:8090")
        conn.list_projects = lambda: (_ for _ in ()).throw(OSError("down"))
        repl, _out = _wire_repl(client, [conn])
        repl.run(io.StringIO("/prompt hello\n"))
        system = client.systems[0]
        self.assertIn("Connections (1): http://a.example:8090.", system)
        self.assertNotIn("Projects on http://a.example:8090", system)


class ReplPlanConsoleOpsOfflineTest(unittest.TestCase):
    """§10 console ops executed from a plan, no native core needed (no image)."""

    def setUp(self) -> None:
        self._dir = tempfile.TemporaryDirectory()
        self._old_cwd = os.getcwd()
        os.chdir(self._dir.name)

    def tearDown(self) -> None:
        os.chdir(self._old_cwd)
        self._dir.cleanup()

    def test_plan_delete_runs_the_same_guards_as_the_command(self) -> None:
        with open("old.stencil", "w", encoding="utf-8") as fh:
            fh.write("{}")
        plan = ('{"version":1,"reply":"tidy","actions":['
                '{"op":"delete","path":"old.stencil"},'
                '{"op":"delete","path":"../escape.stencil"}]}')
        repl, out = _wire_repl(_MockLlmClient(plan))
        repl.run(io.StringIO("/prompt remove the old project\n"))
        text = out.getvalue()
        self.assertIn("deleted old.stencil", text)
        self.assertFalse(os.path.exists("old.stencil"))
        # The traversal guard fires as the /delete command's own message, and the
        # plan carries on (a miss is never a failed plan).
        self.assertIn("refusing to delete a path that escapes the working directory", text)
        self.assertIn("tidy", text)

    def test_plan_connect_and_disconnect_resolve_only_live_servers(self) -> None:
        plan = ('{"version":1,"reply":"managing","actions":['
                '{"op":"disconnect","server":"a.example"},'
                '{"op":"connect","server":"b.example"}]}')
        repl, out = _wire_repl(_MockLlmClient(plan), ["http://a.example:8090"])
        repl.run(io.StringIO("/prompt tidy my connections\n"))
        text = out.getvalue()
        self.assertIn("disconnected from http://a.example:8090", text)
        self.assertEqual(repl._manager.connections, [])
        # The model can never introduce a host: the unknown connect is a note.
        self.assertIn('[warning] skipped connect — "b.example" is not a server you '
                      "connected this session", text)

    def test_plan_connect_to_a_live_server_is_already_connected(self) -> None:
        plan = ('{"version":1,"reply":"on it","actions":['
                '{"op":"connect","server":"a.example"}]}')
        repl, out = _wire_repl(_MockLlmClient(plan), ["http://a.example:8090"])
        repl.run(io.StringIO("/prompt connect to a.example\n"))
        self.assertIn("already connected to http://a.example:8090", out.getvalue())

    def test_ambiguous_disconnect_is_a_note(self) -> None:
        plan = ('{"version":1,"reply":"hm","actions":['
                '{"op":"disconnect","server":"a.example"}]}')
        repl, out = _wire_repl(
            _MockLlmClient(plan), ["http://a.example:8090", "https://a.example"]
        )
        repl.run(io.StringIO("/prompt drop a.example\n"))
        self.assertIn('[warning] skipped disconnect — "a.example" matches several '
                      "connected servers", out.getvalue())
        self.assertEqual(len(repl._manager.connections), 2)

    def test_untyped_open_url_blocks_the_whole_plan(self) -> None:
        with open("bait.stencil", "w", encoding="utf-8") as fh:
            fh.write("{}")
        plan = ('{"version":1,"reply":"fetching","actions":['
                '{"op":"openUrl","url":"https://evil.example/x.png"},'
                '{"op":"delete","path":"bait.stencil"}]}')
        repl, out = _wire_repl(_MockLlmClient(plan))
        repl.run(io.StringIO("/prompt load the cat picture\n"))
        text = out.getvalue()
        self.assertIn(
            'error: openUrl blocked: "https://evil.example/x.png" is not a URL you '
            "gave in this conversation",
            text,
        )
        # NOTHING executed — not even the later delete.
        self.assertTrue(os.path.exists("bait.stencil"))
        self.assertNotIn("fetching", text)


class ReplUploadAttachmentsNativeTest(unittest.TestCase):
    """§2.1 in the REPL: /uploads register as the turn's attachments, ride the wire,
    and make `image` plans work end-to-end (native core: PNG encode/decode)."""

    @classmethod
    def setUpClass(cls) -> None:
        try:
            from pystencil.core import get_core

            get_core()
        except Exception as e:  # pragma: no cover - environment-dependent
            raise unittest.SkipTest("native core unavailable: %s" % e)

    def setUp(self) -> None:
        self._dir = tempfile.TemporaryDirectory()
        self._old_cwd = os.getcwd()
        os.chdir(self._dir.name)
        Editor().blank(10, 8).save("a.png")
        Editor().blank(6, 4).save("b.png")

    def tearDown(self) -> None:
        os.chdir(self._old_cwd)
        self._dir.cleanup()

    def test_uploads_ride_and_an_image_op_switches_and_saves(self) -> None:
        plan = ('{"version":1,"reply":"switched","actions":['
                '{"op":"image","index":1},{"op":"save"}]}')
        client = _MockLlmClient(plan)
        repl, out = _wire_repl(client)
        repl.run(io.StringIO("/upload a.png\n/upload b.png\n/prompt keep the first one\n"))
        # The wire: working snapshot, its edge map, then BOTH uploads in order.
        images = client.sent[0][0]["images"]
        self.assertEqual(len(images), 4)
        self.assertEqual(codecs.decode(bytes(images[2][1]))[:2], (10, 8))
        self.assertEqual(codecs.decode(bytes(images[3][1]))[:2], (6, 4))
        # The `image` op switched the editor to attachment 1 (a.png)…
        self.assertEqual(repl._editor.image_size, (10, 8))
        # …and the unnamed `save` derived its project name from it.
        self.assertIn("saved project a.stencil", out.getvalue())
        self.assertTrue(os.path.exists("a.stencil"))

    def test_an_index_the_turn_cannot_satisfy_is_a_warning(self) -> None:
        plan = ('{"version":1,"reply":"hm","actions":[{"op":"image","index":5}]}')
        repl, out = _wire_repl(_MockLlmClient(plan))
        repl.run(io.StringIO("/upload a.png\n/upload b.png\n/prompt use the fifth\n"))
        self.assertIn("Skipped switching to attached image 5", out.getvalue())
        self.assertEqual(repl._editor.image_size, (6, 4))  # still b.png

    def test_a_single_upload_rides_nothing_extra(self) -> None:
        client = _MockLlmClient("just words")
        repl, _out = _wire_repl(client)
        repl.run(io.StringIO("/upload a.png\n/prompt describe this\n"))
        self.assertEqual(len(client.sent[0][0]["images"]), 2)  # snapshot + edge map

    def test_a_prompt_consumes_the_set_and_the_next_upload_starts_fresh(self) -> None:
        repl, _out = _wire_repl(_MockLlmClient("ok"))
        repl.run(io.StringIO("/upload a.png\n/upload b.png\n/prompt hi\n"))
        self.assertEqual(len(repl._attachments), 2)  # spent, but still recorded
        repl.run(io.StringIO("/upload a.png\n"))
        self.assertEqual([lab for _mt, _d, lab in repl._attachments], ["a.png"])

    def test_the_upload_set_is_capped_at_eight(self) -> None:
        from pystencil.llm import MAX_UPLOAD_ATTACHMENTS

        repl, _out = _wire_repl(_MockLlmClient("ok"))
        repl.run(io.StringIO("/blank 4 4\n"))
        for i in range(MAX_UPLOAD_ATTACHMENTS + 1):
            repl._add_attachment("u%d.png" % i)
        labels = [lab for _mt, _d, lab in repl._attachments]
        self.assertEqual(len(labels), MAX_UPLOAD_ATTACHMENTS)
        self.assertEqual(labels[0], "u1.png")  # the oldest fell off


class ReplOpenUrlNativeTest(unittest.TestCase):
    """§10 openUrl end-to-end in the REPL: the echoed URL loads through the /upload
    path (Editor._fetch_url patched — no network), then §7 auto-continuation."""

    URL = "https://pics.example/cat.png"

    @classmethod
    def setUpClass(cls) -> None:
        try:
            from pystencil.core import get_core

            get_core()
        except Exception as e:  # pragma: no cover - environment-dependent
            raise unittest.SkipTest("native core unavailable: %s" % e)

    def setUp(self) -> None:
        self._dir = tempfile.TemporaryDirectory()
        self._old_cwd = os.getcwd()
        os.chdir(self._dir.name)

    def tearDown(self) -> None:
        os.chdir(self._old_cwd)
        self._dir.cleanup()

    def _patch_fetch(self, result) -> None:
        orig = Editor.__dict__["_fetch_url"]
        if isinstance(result, Exception):
            def fetch(url, timeout=30.0):
                raise result
        else:
            def fetch(url, timeout=30.0):
                return result
        Editor._fetch_url = staticmethod(fetch)
        self.addCleanup(lambda: setattr(Editor, "_fetch_url", orig))

    def _open_url_plan(self, incognito=False) -> str:
        action = {"op": "openUrl", "url": self.URL}
        if incognito:
            action["incognito"] = True
        return json.dumps({"version": 1, "reply": "loading it", "actions": [action]})

    def test_typed_url_loads_synchronously_and_continues_once(self) -> None:
        self._patch_fetch(Editor().blank(12, 10).result().encode("png"))
        client = _MockLlmClient(
            replies=[self._open_url_plan(incognito=True),
                     '{"version":1,"reply":"there it is","actions":[]}']
        )
        repl, out = _wire_repl(client)
        repl.run(io.StringIO("/prompt load %s and describe it\n" % self.URL))
        text = out.getvalue()
        # incognito is not a console concept — ignored with a note (§10 cli row).
        self.assertIn("note: incognito is not a console concept — loading normally", text)
        self.assertIn('loaded "cat" (12x10)', text)
        self.assertEqual(repl._editor.image_size, (12, 10))
        # §7 auto-continuation: the load-only plan re-sent the turn ONCE, with the
        # fetched picture now riding as the working snapshot.
        self.assertEqual(len(client.sent), 2)
        self.assertIn("[The working image is now", client.sent[1][-1]["text"])
        self.assertEqual(len(client.sent[1][-1]["images"]), 2)  # snapshot + edge map
        self.assertIn("there it is", text)

    def test_chat_history_echo_authorises_a_later_turn(self) -> None:
        self._patch_fetch(Editor().blank(12, 10).result().encode("png"))
        client = _MockLlmClient(
            replies=["noted, send more when ready",
                     self._open_url_plan(),
                     '{"version":1,"reply":"loaded","actions":[]}']
        )
        repl, out = _wire_repl(client)
        repl.run(io.StringIO(
            "/chat on\n/prompt remember %s\n/prompt now load it\n" % self.URL
        ))
        text = out.getvalue()
        self.assertNotIn("openUrl blocked", text)
        self.assertEqual(repl._editor.image_size, (12, 10))

    def test_a_failed_fetch_is_a_note_not_a_dead_turn(self) -> None:
        self._patch_fetch(OSError("boom"))
        client = _MockLlmClient(replies=[self._open_url_plan()])
        repl, out = _wire_repl(client)
        repl.run(io.StringIO("/prompt load %s\n" % self.URL))
        text = out.getvalue()
        self.assertIn("[warning] skipped openUrl — could not load", text)
        self.assertIn("loading it", text)  # the reply still printed
        self.assertFalse(repl._editor.has_image())

    def test_plan_clear_drops_the_image_in_place(self) -> None:
        plan = '{"version":1,"reply":"gone","actions":[{"op":"clear"}]}'
        repl, out = _wire_repl(_MockLlmClient(plan))
        repl.run(io.StringIO("/blank 16 16\n/prompt remove the image\n"))
        text = out.getvalue()
        self.assertIn("dropped the working image", text)
        self.assertIn("applied 1 action(s)", text)
        self.assertFalse(repl._editor.has_image())
        self.assertIn("gone", text)


class SeverityPrefixTest(unittest.TestCase):
    """The console's severity vocabulary (pystencil/_severity.py) — the Python twin of
    the Zig CLI's logo.err()/logo.note(). Needs no core: nothing here decodes an image."""

    class _Tty(io.StringIO):
        """A capture buffer that claims to be a terminal, so colour turns on."""

        def isatty(self) -> bool:
            return True

    def setUp(self) -> None:
        self._no_color = os.environ.pop("NO_COLOR", None)

    def tearDown(self) -> None:
        if self._no_color is None:
            os.environ.pop("NO_COLOR", None)
        else:
            os.environ["NO_COLOR"] = self._no_color

    def test_plain_when_the_stream_is_not_a_terminal(self) -> None:
        out = io.StringIO()
        self.assertEqual(
            _severity.error_line("cannot read 'a.png'", out), "error: cannot read 'a.png'"
        )
        self.assertEqual(_severity.note_line("skipped save", out), "note: skipped save")

    def test_colours_only_the_prefix_on_a_terminal(self) -> None:
        tty = self._Tty()
        self.assertEqual(
            _severity.error_line("boom", tty), "\x1b[1;38;2;239;68;68merror: \x1b[0mboom"
        )
        self.assertEqual(
            _severity.note_line("hm", tty), "\x1b[1;38;2;245;158;11mnote: \x1b[0mhm"
        )

    def test_no_color_wins_over_the_terminal(self) -> None:
        os.environ["NO_COLOR"] = "1"
        self.assertEqual(_severity.error_line("boom", self._Tty()), "error: boom")

    def test_repl_lines_stay_byte_for_byte_plain_off_a_terminal(self) -> None:
        out = io.StringIO()
        cli._Repl(out).run(io.StringIO("/upload\n/nope\n"))
        self.assertEqual(
            out.getvalue(),
            "error: /upload needs a path or URL\n"
            "error: unknown command '/nope' (try /help)\n",
        )

    def test_repl_colours_the_prefix_on_a_terminal(self) -> None:
        tty = self._Tty()
        cli._Repl(tty).run(io.StringIO("/upload\n"))
        self.assertEqual(
            tty.getvalue(),
            "\x1b[1;38;2;239;68;68merror: \x1b[0m/upload needs a path or URL\n",
        )

    def test_one_shot_pipeline_errors_go_through_the_helper(self) -> None:
        err = io.StringIO()
        with contextlib.redirect_stderr(err):
            code = cli.main(["--input", "a.png", "--blank", "10", "10", "out.png"])
        self.assertEqual(code, 2)
        self.assertEqual(err.getvalue(), "error: --input and --blank are mutually exclusive\n")

    def test_listing_answers_plain_and_actions_refuse_with_error(self) -> None:
        # Same condition, same wording as the Zig console (cli/src/console/handlers.zig):
        # /connections truthfully answering "there are none" is not a refusal, /disconnect is.
        out = io.StringIO()
        cli._Repl(out).run(io.StringIO("/connections\n/disconnect\n"))
        self.assertEqual(
            out.getvalue(),
            "no server connections — use '/connect <url>'\n"
            "error: no server connections — use '/connect <url>'\n",
        )

    def test_console_sources_use_the_helper_not_the_literal(self) -> None:
        # Single-sourcing guard: a new call site must go through _severity, not re-type
        # the prefix (which would silently opt out of the colouring).
        for mod in (cli, sitesource):
            with open(mod.__file__, encoding="utf-8") as fh:
                src = fh.read()
            for literal in ('"error: ', "'error: ", '"note: ', "'note: ", '"warning: '):
                self.assertNotIn(literal, src, "%s re-types %r" % (mod.__name__, literal))
