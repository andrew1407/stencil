"""Chat.from_doc / Chat.to_doc over the §12.1 vectors.

Part of the cross-surface fixture-conformance walk; the corpus roots and the
shared helpers live in :mod:`tests.fixturebase`.
"""

from __future__ import annotations

import json
import unittest

from tests.fixturebase import _LLM_FIXTURES, _load

from pystencil.llm import Chat

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


if __name__ == "__main__":
    unittest.main()
