"""The Ollama and OpenAI-compatible request shapes, and the LLM call's own timeout."""

from __future__ import annotations

import base64
import json
import sys
import unittest
from pathlib import Path

# Make the package importable when running `python3 -m unittest` from pystencil/.
_PKG_ROOT = Path(__file__).resolve().parent.parent
if str(_PKG_ROOT) not in sys.path:
    sys.path.insert(0, str(_PKG_ROOT))

from pystencil.llm import LLM_SYSTEM_PROMPT, LlmClient, LlmConfig, LlmError
from tests.stubs import _plan_json


class OllamaRequestTest(unittest.TestCase):
    """Contract §6.1: POST {baseUrl}/api/chat, native chat, bare-base64 images."""

    def setUp(self) -> None:
        self.client = LlmClient(LlmConfig(provider="ollama", model="llava"))

    def test_request_shape(self) -> None:
        msgs = [{"role": "user", "text": "hi", "images": [("image/png", b"hi")]}]
        req = self.client._build_request(msgs)
        self.assertEqual(req.get_method(), "POST")
        self.assertEqual(req.full_url, "http://localhost:11434/api/chat")
        self.assertEqual(req.get_header("Content-type"), "application/json")
        self.assertIsNone(req.get_header("Authorization"))  # never a key on ollama
        body = json.loads(req.data.decode("utf-8"))
        self.assertEqual(body["model"], "llava")
        self.assertIs(body["stream"], False)
        self.assertEqual(
            body["messages"][0], {"role": "system", "content": LLM_SYSTEM_PROMPT}
        )
        self.assertEqual(
            body["messages"][1],
            {
                "role": "user",
                "content": "hi",
                "images": [base64.b64encode(b"hi").decode("ascii")],
            },
        )

    def test_text_only_message_has_no_images_key(self) -> None:
        req = self.client._build_request([{"role": "assistant", "text": "prior"}])
        body = json.loads(req.data.decode("utf-8"))
        self.assertNotIn("images", body["messages"][1])

    def test_chat_extracts_message_content(self) -> None:
        captured = {}

        def stub_open(req):
            captured["url"] = req.full_url
            return {"message": {"content": _plan_json(reply="done")}}

        self.client._open = stub_open
        text = self.client.chat([{"role": "user", "text": "go"}])
        self.assertEqual(captured["url"], "http://localhost:11434/api/chat")
        self.assertEqual(json.loads(text)["reply"], "done")

    def test_malformed_response_raises(self) -> None:
        self.client._open = lambda req: {"nope": True}
        with self.assertRaises(LlmError):
            self.client.chat([{"role": "user", "text": "go"}])


class OpenAiCompatRequestTest(unittest.TestCase):
    """Contract §6.2: POST {baseUrl}/chat/completions, data-URL images, Bearer key."""

    def test_request_shape_with_bearer_and_image(self) -> None:
        client = LlmClient(
            LlmConfig(provider="openai-compat", model="qwen", api_key="sk-1")
        )
        msgs = [{"role": "user", "text": "hi", "images": [("image/jpeg", b"hi")]}]
        req = client._build_request(msgs)
        self.assertEqual(req.full_url, "http://localhost:1234/v1/chat/completions")
        self.assertEqual(req.get_header("Authorization"), "Bearer sk-1")
        body = json.loads(req.data.decode("utf-8"))
        self.assertIs(body["stream"], False)
        self.assertEqual(
            body["messages"][0], {"role": "system", "content": LLM_SYSTEM_PROMPT}
        )
        self.assertEqual(
            body["messages"][1]["content"],
            [
                {"type": "text", "text": "hi"},
                {
                    "type": "image_url",
                    "image_url": {"url": "data:image/jpeg;base64,aGk="},
                },
            ],
        )

    def test_no_api_key_omits_authorization(self) -> None:
        # LM Studio needs no key — the header must be absent, not "Bearer ".
        client = LlmClient(LlmConfig(provider="openai-compat"))
        req = client._build_request([{"role": "user", "text": "hi"}])
        self.assertIsNone(req.get_header("Authorization"))
        body = json.loads(req.data.decode("utf-8"))
        # Text-only content is a plain string, not a parts array.
        self.assertEqual(body["messages"][1]["content"], "hi")

    def test_chat_extracts_first_choice(self) -> None:
        client = LlmClient(LlmConfig(provider="openai-compat"))
        client._open = lambda req: {
            "choices": [{"message": {"content": "plain text answer"}}]
        }
        self.assertEqual(
            client.chat([{"role": "user", "text": "go"}]), "plain text answer"
        )


class LlmTimeoutTest(unittest.TestCase):
    """A vision op-plan runs for a minute or two, so the LLM seam must not inherit the
    REST timeout — it opens under the 120 s every other client allows."""

    def test_chat_opens_under_the_llm_timeout_not_the_rest_one(self) -> None:
        from pystencil.server import http as server_mod

        self.assertGreaterEqual(server_mod._LLM_TIMEOUT, 120.0)
        self.assertGreater(server_mod._LLM_TIMEOUT, server_mod._REQUEST_TIMEOUT)
        seen: list = []

        class _Resp:
            status = 200

            def getcode(self):
                return 200

            def read(self):
                return json.dumps({"model": "m", "text": "ok", "stopReason": "end_turn"}).encode()

            def __enter__(self):
                return self

            def __exit__(self, *a):
                return False

        opener = server_mod._no_redirect_opener
        server_mod._no_redirect_opener = type(
            "O", (), {"open": staticmethod(lambda req, timeout: seen.append(timeout) or _Resp())})
        try:
            client = LlmClient(
                LlmConfig(provider="stencil-server", server_url="https://srv:8090"), token="t")
            self.assertEqual(client.chat([{"role": "user", "text": "go"}]), "ok")
        finally:
            server_mod._no_redirect_opener = opener
        self.assertEqual(seen, [server_mod._LLM_TIMEOUT])
