"""Round-trip tests for the portable .stencil project file
(Editor.save_project / open_project). Needs the native core to render; self-skips without it.
"""

from __future__ import annotations

import json
import os
import tempfile
import unittest

from pystencil.editor import Editor


class ProjectFileTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        try:
            from pystencil.core import get_core

            get_core()
        except Exception as exc:  # noqa: BLE001 - any failure means "no native lib"
            raise unittest.SkipTest("native core unavailable: %s" % exc)

    def _authored(self) -> Editor:
        """A blue blank with a name/colour/provenance, one quarter-turn, and a drawn line."""
        ed = Editor().blank(20, 12, color="#3060c0")
        ed._name = "proj"
        ed._color = "#7c3aed"
        ed._source = "https://example.com/a.png"
        ed.rotate_right()  # rotationQuarters = 1
        ed.draw({"lines": [{"points": [{"x": 1, "y": 1}, {"x": 5, "y": 5}], "color": "#ff0000"}]})
        return ed

    def test_round_trip_file(self):
        ed = self._authored()
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, "p.stencil")
            self.assertEqual(ed.save_project(path), path)
            with open(path) as fh:
                doc = json.load(fh)
            self.assertEqual(doc["format"], "stencil-project")
            self.assertEqual(doc["version"], 1)
            self.assertEqual(doc["name"], "proj")
            self.assertEqual(doc["color"], "#7c3aed")
            self.assertEqual(doc["source"], "https://example.com/a.png")
            self.assertTrue(doc["image"]["dataUrl"].startswith("data:image/png;base64,"))

            ed2 = Editor().open_project(path)
            self.assertEqual(ed2.name, "proj")
            self.assertEqual(ed2._color, "#7c3aed")
            layout = ed2.layout()
            self.assertEqual(layout.rotation_quarters, 1)
            self.assertEqual(len(layout.lines), 1)
            self.assertEqual(ed2.image_size, ed.image_size)  # same rendered dims

    def test_open_from_bytes_and_dict(self):
        ed = self._authored()
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, "p.stencil")
            ed.save_project(path)
            with open(path, "rb") as fh:
                raw = fh.read()
            self.assertTrue(Editor().open_project(raw).has_image())  # raw JSON bytes
            self.assertTrue(Editor().open_project(json.loads(raw)).has_image())  # parsed dict

    def test_keywords_round_trip(self):
        ed = self._authored()
        # set_keywords trims and drops empties/non-strings (mirrors projectFile.js cleanKeywords).
        ed.set_keywords(["  alpha ", "beta", "", 5, "alpha"])
        self.assertEqual(ed.keywords, ["alpha", "beta", "alpha"])
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, "p.stencil")
            ed.save_project(path)
            with open(path) as fh:
                doc = json.load(fh)
            self.assertEqual(doc["keywords"], ["alpha", "beta", "alpha"])
            self.assertEqual(Editor().open_project(path).keywords, ["alpha", "beta", "alpha"])

    def test_keywords_omitted_when_empty(self):
        ed = self._authored()  # no keywords set
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, "p.stencil")
            ed.save_project(path)
            with open(path) as fh:
                doc = json.load(fh)
            self.assertNotIn("keywords", doc)
            self.assertEqual(Editor().open_project(path).keywords, [])

    def test_preserves_foreign_keywords_on_reopen(self):
        # A .stencil authored by another surface (browser/server) carries keywords pystencil
        # never set; opening then re-saving must not drop them. This is the interop regression
        # the fix guards — before it, save_project/open_project ignored keywords entirely.
        ed = self._authored()
        with tempfile.TemporaryDirectory() as d:
            src = os.path.join(d, "src.stencil")
            ed.save_project(src)
            with open(src) as fh:
                doc = json.load(fh)
            doc["keywords"] = ["from-browser", "shared"]
            resaved = os.path.join(d, "out.stencil")
            Editor().open_project(doc).save_project(resaved)
            with open(resaved) as fh:
                out = json.load(fh)
            self.assertEqual(out["keywords"], ["from-browser", "shared"])

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

    def test_delete_project(self):
        # Parity with the browser/desktop trash button + CLI /delete: remove the file from disk,
        # while a loaded editor stays loaded (delete_project is stateless).
        ed = self._authored()
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, "p.stencil")
            ed.save_project(path)
            self.assertTrue(os.path.exists(path))
            self.assertEqual(Editor.delete_project(path), path)
            self.assertFalse(os.path.exists(path))
            self.assertTrue(ed.has_image())  # the open project is untouched

    def test_delete_project_rejects_non_stencil(self):
        with tempfile.TemporaryDirectory() as d:
            other = os.path.join(d, "notes.txt")
            with open(other, "w") as fh:
                fh.write("keep me")
            with self.assertRaises(ValueError):
                Editor.delete_project(other)
            self.assertTrue(os.path.exists(other))  # a non-.stencil file is never removed

    def test_delete_project_missing_raises(self):
        with tempfile.TemporaryDirectory() as d:
            with self.assertRaises(FileNotFoundError):
                Editor.delete_project(os.path.join(d, "gone.stencil"))

    def test_rejects_foreign_or_too_new(self):
        with self.assertRaises(ValueError):
            Editor().open_project('{"version":1}')  # no format sentinel
        with self.assertRaises(ValueError):
            Editor().open_project(
                '{"format":"stencil-project","version":999,'
                '"image":{"dataUrl":"data:image/png;base64,AAAA"}}'
            )  # too new
        with self.assertRaises(ValueError):
            Editor().open_project('{"format":"stencil-project","version":1}')  # no image


if __name__ == "__main__":
    unittest.main()
