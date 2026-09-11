"""The .stencil file's §12 persisted-chat block: opt-in, tolerant on reopen."""

from __future__ import annotations

import json
import os
import tempfile
import unittest

from tests.projectfilecase import ProjectFileCase

from pystencil.editor import Editor
from pystencil.llm import Chat


class ProjectFileChatTests(ProjectFileCase):
    def _chat_doc(self) -> dict:
        """A minimal valid §12.1 persisted-chat document."""
        return {
            "version": 1,
            "savedAt": 123,
            "messages": [
                {"role": "user", "text": "crop 10% off the left"},
                {"role": "assistant", "text": "Done — anything else?"},
            ],
        }

    def test_chat_omitted_by_default(self):
        # §12 is opt-in and OFF by default: even an attached conversation is not
        # written while save_chats stays False.
        ed = self._authored()
        ed.attach_chat(self._chat_doc())
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, "p.stencil")
            ed.save_project(path)
            with open(path) as fh:
                self.assertNotIn("chat", json.load(fh))

    def test_chat_round_trips_when_opted_in(self):
        ed = self._authored()
        ed.save_chats = True
        ed.attach_chat(self._chat_doc())
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, "p.stencil")
            ed.save_project(path)
            with open(path) as fh:
                self.assertEqual(json.load(fh)["chat"], self._chat_doc())
            # open_project restores the block onto chat_doc for a later re-save.
            self.assertEqual(Editor().open_project(path).chat_doc, self._chat_doc())

    def test_chat_omitted_without_messages(self):
        # The omit-when-empty convention (like keywords): the toggle alone, or an
        # empty conversation, writes no `chat` key.
        ed = self._authored()
        ed.save_chats = True
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, "p.stencil")
            ed.save_project(path)
            with open(path) as fh:
                self.assertNotIn("chat", json.load(fh))
            ed.attach_chat({"version": 1, "savedAt": 1, "messages": []})
            ed.save_project(path)
            with open(path) as fh:
                self.assertNotIn("chat", json.load(fh))

    def test_attach_chat_accepts_a_chat_instance(self):
        # attach_chat serializes a live pystencil.llm.Chat via its to_doc().
        from pystencil.llm import Chat

        chat = Chat(client=object())  # never used: attach/save trigger no model call
        chat.history.append({"role": "user", "text": "hello", "images": []})
        ed = self._authored()
        ed.save_chats = True
        ed.attach_chat(chat)
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, "p.stencil")
            ed.save_project(path)
            with open(path) as fh:
                doc = json.load(fh)
            self.assertEqual(doc["chat"]["messages"], [{"role": "user", "text": "hello"}])

    def test_dirty_chat_block_does_not_round_trip(self):
        # §12.1: the block is shared across surfaces, so §7's continuation note and a raw
        # op-plan assistant turn are refused both when a file is opened and when one is
        # written — an older build's dirty chat never comes back out of save_project.
        from pystencil.llm import CONTINUATION_NOTE

        dirty = {
            "version": 1,
            "savedAt": 5,
            "messages": [
                {"role": "user", "text": "[The working image is now the frame — carry on.]"},
                {"role": "user", "text": "crop it\n\n%s" % CONTINUATION_NOTE},
                {"role": "assistant", "text": '{"version":1,"reply":"Cropped.","actions":[]}'},
                {"role": "assistant", "text": "Cropped."},
            ],
        }
        ed = self._authored()
        ed.save_chats = True
        ed.attach_chat(dirty)
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, "p.stencil")
            ed.save_project(path)
            with open(path) as fh:
                saved = json.load(fh)
            clean = [{"role": "user", "text": "crop it"}, {"role": "assistant", "text": "Cropped."}]
            self.assertEqual(saved["chat"]["messages"], clean)
            # Opening a file that still holds a dirty block cleans it on the way in, too.
            saved["chat"] = dirty
            self.assertEqual(Editor().open_project(saved).chat_doc["messages"], clean)

    def test_invalid_chat_block_is_ignored(self):
        # An unknown-versioned or malformed `chat` block reads as "no saved chat"
        # (chat_doc None), silently — the format's unknown-key tolerance.
        ed = self._authored()
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, "p.stencil")
            ed.save_project(path)
            with open(path) as fh:
                doc = json.load(fh)
        for bad in ({"version": 99, "messages": []}, {"version": 1}, ["not", "a", "doc"], "x"):
            doc["chat"] = bad
            self.assertIsNone(Editor().open_project(doc).chat_doc, bad)


if __name__ == "__main__":
    unittest.main()
