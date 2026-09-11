"""What a /prompt turn puts on the wire: the memoised working-image encode, the size cap,
and the edge map that rides beside it with its system-prompt sentence.
"""

from __future__ import annotations

import sys
from pathlib import Path

# Make the package importable when running `python3 -m unittest` from pystencil/.
_PKG_ROOT = Path(__file__).resolve().parent.parent
if str(_PKG_ROOT) not in sys.path:
    sys.path.insert(0, str(_PKG_ROOT))

import io

from pystencil import cli
from pystencil import codecs
from pystencil.cli.commands import prompt as prompt_mod
from pystencil.llm import CONSOLE_SYSTEM_PROMPT, EDGE_MAP_SUFFIX, LlmConfig

from tests.clicase import _MockLlmClient, _NativeReplCase


class ReplPromptPayloadTest(_NativeReplCase):
    """The attachments and system prompt a /prompt turn sends (native: PNG encode)."""

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
        original = prompt_mod._PROMPT_IMAGE_LIMIT
        prompt_mod._PROMPT_IMAGE_LIMIT = 16  # force the "too large" branch cheaply
        try:
            repl.run(io.StringIO("/blank 32 32\n/prompt describe this\n"))
        finally:
            prompt_mod._PROMPT_IMAGE_LIMIT = original
        text = out.getvalue()
        self.assertIn("sending text only", text)
        self.assertEqual(client.sent[0][0]["images"], [])  # attachment skipped
        # Snapshot dropped ⇒ edge map dropped too, so no edge-map sentence (the
        # console context always rides the spliced console prompt).
        self.assertTrue(client.systems[0].startswith(CONSOLE_SYSTEM_PROMPT))
        self.assertNotIn(EDGE_MAP_SUFFIX, client.systems[0])

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

    def test_oversized_edge_map_alone_is_dropped(self) -> None:
        client = _MockLlmClient("noted")
        repl, _out = self._repl(client)
        repl._edge_map_png = lambda: b"e" * (prompt_mod._PROMPT_IMAGE_LIMIT + 1)
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
