"""§10 console ops executed from a plan with no native core, plus the §7 continuation
predicate that decides whether a load-only plan earns a second model round.
"""

from __future__ import annotations

import io
import os
import unittest

from tests.clicase import _MockLlmClient, _CwdCase, _wire_repl


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


class ReplPlanConsoleOpsOfflineTest(_CwdCase):
    """§10 console ops executed from a plan, no native core needed (no image)."""

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
