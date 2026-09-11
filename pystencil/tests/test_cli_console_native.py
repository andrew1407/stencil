"""§2.1 /upload attachments and §10 openUrl driven end-to-end in the REPL, with the native
core doing the PNG work and Editor._fetch_url patched so nothing leaves the machine.
"""

from __future__ import annotations

import sys
from pathlib import Path

# Make the package importable when running `python3 -m unittest` from pystencil/.
_PKG_ROOT = Path(__file__).resolve().parent.parent
if str(_PKG_ROOT) not in sys.path:
    sys.path.insert(0, str(_PKG_ROOT))

import io
import json
import os

from pystencil import codecs
from pystencil.editor import Editor

from tests.clicase import _MockLlmClient, _NativeReplCase, _wire_repl


class ReplUploadAttachmentsNativeTest(_NativeReplCase):
    """§2.1 in the REPL: /uploads register as the turn's attachments, ride the wire,
    and make `image` plans work end-to-end (native core: PNG encode/decode)."""

    def setUp(self) -> None:
        super().setUp()
        Editor().blank(10, 8).save("a.png")
        Editor().blank(6, 4).save("b.png")
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


class ReplOpenUrlNativeTest(_NativeReplCase):
    """§10 openUrl end-to-end in the REPL: the echoed URL loads through the /upload
    path (Editor._fetch_url patched — no network), then §7 auto-continuation."""

    URL = "https://pics.example/cat.png"

    def _patch_fetch(self, result) -> None:
        from pystencil.editor.source import _SourceApi

        orig = _SourceApi.__dict__["_fetch_url"]
        if isinstance(result, Exception):
            def fetch(url, timeout=30.0):
                raise result
        else:
            def fetch(url, timeout=30.0):
                return result
        _SourceApi._fetch_url = staticmethod(fetch)
        self.addCleanup(lambda: setattr(_SourceApi, "_fetch_url", orig))

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
