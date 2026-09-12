"""Stateful chat (contract §7): history replay, attachment caps, transient images."""

from __future__ import annotations

import unittest

from pystencil.llm import (
  Chat,
  EDGE_MAP_SUFFIX,
  LLM_SYSTEM_PROMPT,
  MAX_ATTACHMENTS,
  MAX_HISTORY,
  OpPlan,
)
from tests.stubs import _StubClient, _plan_json


class ChatTest(unittest.TestCase):
  def test_send_returns_reply_and_plan(self) -> None:
    client = _StubClient(_plan_json(reply="hi there"))
    chat = Chat(client)
    reply, plan = chat.send("hello")
    self.assertEqual(reply, "hi there")
    self.assertIsInstance(plan, OpPlan)
    # History keeps the user turn and the RAW assistant reply (the model must
    # see its own JSON on later turns).
    self.assertEqual(len(chat.history), 2)
    self.assertEqual(chat.history[0]["role"], "user")
    self.assertEqual(chat.history[1]["role"], "assistant")
    self.assertEqual(chat.history[1]["text"], _plan_json(reply="hi there"))

  def test_history_is_bounded_to_32_messages(self) -> None:
    chat = Chat(_StubClient(_plan_json()))
    for i in range(40):
      chat.send("turn %d" % i)
    self.assertEqual(len(chat.history), MAX_HISTORY)
    # The retained window is the most recent one (oldest turns evicted).
    self.assertEqual(chat.history[-2]["text"], "turn 39")

  def test_image_replay_rule(self) -> None:
    client = _StubClient(_plan_json())
    chat = Chat(client)
    a1, a2 = ("image/png", b"a1"), ("image/png", b"a2")
    b1 = ("image/jpeg", b"b1")
    chat.send("first", images=[a1, a2])
    chat.send("second", images=[b1])
    chat.send("third")

    # Turn 2: current turn's images + the single most recent prior image (a2).
    wire2 = client.sent[1]
    self.assertEqual(wire2[0]["images"], [a2])  # only the LAST prior image
    self.assertEqual(wire2[2]["images"], [b1])  # current turn keeps its own
    # Turn 3: no current images; only b1 (most recent prior) is replayed.
    wire3 = client.sent[2]
    self.assertEqual([m["images"] for m in wire3], [[], [], [b1], [], []])
    # Memory: once turn 2's image was on the wire, turn 1's attachments can
    # never be replayed again, so history no longer retains them; the newest
    # image-bearing turn keeps its own (b1 may still replay later).
    self.assertEqual(chat.history[0]["images"], [])
    self.assertEqual(chat.history[2]["images"], [b1])

  def test_rejects_unsupported_media_type(self) -> None:
    chat = Chat(_StubClient(_plan_json()))
    with self.assertRaises(ValueError):
      chat.send("bad", images=[("image/tiff", b"x")])
    with self.assertRaises(ValueError):
      chat.send("bad", images=[b"just bytes"])
    self.assertEqual(chat.history, [])  # a rejected turn never joins history

  def test_chat_only_reply_round_trips(self) -> None:
    chat = Chat(_StubClient("No JSON, just words."))
    reply, plan = chat.send("chat?")
    self.assertEqual(reply, "No JSON, just words.")
    self.assertEqual(plan.actions, [])


class AttachmentCapTests(unittest.TestCase):
  """Contract §7: at most MAX_ATTACHMENTS images per message."""

  def _img(self, n):
    return [("image/png", b"x%d" % i) for i in range(n)]

  def test_up_to_the_cap_passes_through(self):
    chat = Chat(client=_StubClient('{"version":1,"reply":"ok","actions":[],"variants":[]}'))
    for n in range(MAX_ATTACHMENTS + 1):
      self.assertEqual(len(chat._check_images(self._img(n))), n)

  def test_over_the_cap_is_an_error_not_a_silent_trim(self):
    chat = Chat(client=_StubClient('{"version":1,"reply":"ok","actions":[],"variants":[]}'))
    with self.assertRaises(ValueError) as cm:
      chat._check_images(self._img(MAX_ATTACHMENTS + 1))
    self.assertIn("up to %d images" % MAX_ATTACHMENTS, str(cm.exception))

  def test_the_cap_matches_the_other_clients(self):
    self.assertEqual(MAX_ATTACHMENTS, 3)


class ChatSystemAndTransientTest(unittest.TestCase):
  """Chat.send threading for §7's edge map: a per-call system prompt and
  transient images that ride the current turn only, never the replay."""

  def test_edge_map_suffix_is_the_contract_sentence(self):
    self.assertEqual(
      EDGE_MAP_SUFFIX,
      "The second attached image is an edge-map render of the working image "
      "at the same pixel coordinates: use it to place outline points on real "
      "edges.",
    )

  def test_system_override_is_per_call_only(self):
    client = _StubClient(_plan_json())
    chat = Chat(client)
    chat.send("one", system=LLM_SYSTEM_PROMPT + "\n\n" + EDGE_MAP_SUFFIX)
    chat.send("two")
    self.assertTrue(client.systems[0].endswith(EDGE_MAP_SUFFIX))
    self.assertEqual(client.systems[1], LLM_SYSTEM_PROMPT)  # not remembered

  def test_transient_images_ride_current_turn_and_never_replay(self):
    client = _StubClient(_plan_json())
    chat = Chat(client)
    snap1, edge1 = ("image/png", b"snap1"), ("image/png", b"edge1")
    snap2, edge2 = ("image/png", b"snap2"), ("image/png", b"edge2")
    chat.send("one", images=[snap1], transient_images=[edge1])
    # Turn 1's wire carries snapshot then edge map, in that order; history
    # keeps only the snapshot — the edge map is never stored.
    self.assertEqual(client.sent[0][-1]["images"], [snap1, edge1])
    self.assertEqual(chat.history[0]["images"], [snap1])
    chat.send("two", images=[snap2], transient_images=[edge2])
    # Turn 2: the single most recent prior image is the SNAPSHOT, not the
    # edge map; the current turn again carries its own pair.
    self.assertEqual(client.sent[1][0]["images"], [snap1])
    self.assertEqual(client.sent[1][-1]["images"], [snap2, edge2])

  def test_transient_images_are_validated_like_attachments(self):
    chat = Chat(_StubClient(_plan_json()))
    with self.assertRaises(ValueError):
      chat.send("bad", transient_images=[("image/tiff", b"x")])
