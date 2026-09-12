"""§12 chat persistence: the transcript document, its gate, and what round-trips."""

from __future__ import annotations

import json
import unittest

from pystencil.llm import CHAT_DOC_VERSION, CONTINUATION_NOTE, Chat, MAX_HISTORY
from tests.stubs import _StubClient, _plan_json


class ChatPersistenceTest(unittest.TestCase):
    """The §12.1 persisted-chat document: Chat.to_doc / from_doc / clear."""

    def test_to_doc_round_trips_text_only(self) -> None:
        chat = Chat(_StubClient(_plan_json(reply="hi there")))
        chat.send("hello", images=[("image/png", b"pix")])
        doc = chat.to_doc(now_ms=123)
        self.assertEqual(doc["version"], CHAT_DOC_VERSION)
        self.assertEqual(doc["savedAt"], 123)  # informational stamp, pinnable
        # Text-only by contract: no "images" key anywhere, and the assistant turn
        # stores the DISPLAYED reply, never the raw JSON plan it arrived as.
        self.assertEqual(
            doc["messages"],
            [{"role": "user", "text": "hello"}, {"role": "assistant", "text": "hi there"}],
        )
        restored = Chat.from_doc(doc, _StubClient(_plan_json()))
        self.assertEqual(
            restored.history,
            [
                {"role": "user", "text": "hello", "images": []},
                {"role": "assistant", "text": "hi there", "images": []},
            ],
        )

    def test_to_doc_keeps_chat_only_assistant_text(self) -> None:
        # A reply that never parsed as a plan was displayed as-is; persist it as-is.
        chat = Chat(_StubClient("Just words, no plan."))
        chat.send("chat?")
        self.assertEqual(chat.to_doc()["messages"][1]["text"], "Just words, no plan.")

    def test_to_doc_drops_empty_text_turns_and_trims(self) -> None:
        chat = Chat(_StubClient(_plan_json()))
        chat.history.append({"role": "user", "text": "", "images": []})  # dropped
        for i in range(MAX_HISTORY + 4):
            chat.history.append({"role": "user", "text": "turn %d" % i, "images": []})
        doc = chat.to_doc()
        self.assertEqual(len(doc["messages"]), MAX_HISTORY)  # most recent 32 kept
        self.assertEqual(doc["messages"][-1]["text"], "turn %d" % (MAX_HISTORY + 3))

    def test_from_doc_accepts_a_json_string(self) -> None:
        text = json.dumps(
            {"version": 1, "savedAt": 1, "messages": [{"role": "user", "text": "hi"}]}
        )
        chat = Chat.from_doc(text, _StubClient(_plan_json()))
        self.assertEqual(chat.history, [{"role": "user", "text": "hi", "images": []}])

    def test_from_doc_is_forgiving(self) -> None:
        # version != 1, malformed shapes, and bad JSON all mean "empty chat" —
        # never an exception (the contract's treated-as-missing rule).
        client = _StubClient(_plan_json())
        for bad in (
            {"version": 2, "messages": [{"role": "user", "text": "x"}]},
            {"messages": [{"role": "user", "text": "x"}]},  # version absent
            {"version": 1, "messages": "nope"},
            {"version": 1},
            "not json {",
            None,
            42,
        ):
            self.assertEqual(Chat.from_doc(bad, client).history, [], bad)
        # Per-message tolerance: unknown roles / non-string text are dropped; a
        # stray "images" field is ignored (persisted chats are text-only).
        chat = Chat.from_doc(
            {
                "version": 1,
                "messages": [
                    {"role": "system", "text": "dropped"},
                    {"role": "user", "text": 7},
                    {"role": "user", "text": "kept", "images": ["stray"]},
                    "not a dict",
                ],
            },
            client,
        )
        self.assertEqual(chat.history, [{"role": "user", "text": "kept", "images": []}])

    def test_from_doc_truncates_to_max_history(self) -> None:
        doc = {
            "version": 1,
            "messages": [
                {"role": "user", "text": "turn %d" % i} for i in range(MAX_HISTORY + 8)
            ],
        }
        chat = Chat.from_doc(doc, _StubClient(_plan_json()))
        self.assertEqual(len(chat.history), MAX_HISTORY)
        self.assertEqual(chat.history[-1]["text"], "turn %d" % (MAX_HISTORY + 7))

    def test_to_doc_keeps_the_continuation_note_out(self) -> None:
        # §7's continuation round restates the request with the internal note appended;
        # the shared document carries the user's own words only (§12.1).
        chat = Chat(_StubClient(_plan_json(reply="done")))
        chat.send("outline the face")
        chat.send("outline the face\n\n%s" % CONTINUATION_NOTE)
        doc = chat.to_doc()
        self.assertNotIn("The working image is now", json.dumps(doc))
        self.assertEqual([m["text"] for m in doc["messages"] if m["role"] == "user"],
                         ["outline the face", "outline the face"])

    def test_from_doc_sanitizes_an_older_dirty_document(self) -> None:
        # A document written by another surface (or an older build) may carry §7
        # machinery; restoring must never replay it as the user's words or show a raw
        # plan as an assistant reply. A user's OWN pasted JSON still survives.
        chat = Chat.from_doc(
            {
                "version": 1,
                "messages": [
                    {"role": "user", "text": "[The working image is now the frame — carry on.]"},
                    {"role": "user", "text": "crop it\n\n%s" % CONTINUATION_NOTE},
                    {"role": "assistant", "text": _plan_json(reply="Cropped.")},
                    {"role": "user", "text": '{"version":1,"actions":[]}'},
                    {"role": "assistant", "text": "Cropped."},
                ],
            },
            _StubClient(_plan_json()),
        )
        self.assertEqual(
            [(m["role"], m["text"]) for m in chat.history],
            [
                ("user", "crop it"),
                ("user", '{"version":1,"actions":[]}'),
                ("assistant", "Cropped."),
            ],
        )

    def test_clean_document_round_trips_identically(self) -> None:
        doc = {
            "version": 1,
            "savedAt": 7,
            "messages": [
                {"role": "user", "text": "crop 10% off the left"},
                {"role": "assistant", "text": "Done — anything else?"},
            ],
        }
        restored = Chat.from_doc(doc, _StubClient(_plan_json()))
        self.assertEqual(restored.to_doc(now_ms=7), doc)

    def test_clear_empties_the_history(self) -> None:
        client = _StubClient(_plan_json())
        chat = Chat(client)
        chat.send("one")
        chat.clear()
        self.assertEqual(chat.history, [])
        self.assertIs(chat.client, client)  # the client/config survives a clear
