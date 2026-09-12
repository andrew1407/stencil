"""Provider configuration (contract §5) and the variant slug rules."""

from __future__ import annotations

import unittest

from pystencil.llm import LlmConfig, Variant, variant_slug, variant_slugs


class LlmConfigTest(unittest.TestCase):
  def test_defaults_per_provider(self) -> None:
    self.assertEqual(LlmConfig().base_url, "http://localhost:11434")
    self.assertEqual(
      LlmConfig(provider="openai-compat").base_url, "http://localhost:1234/v1"
    )
    # stencil-server has no base_url default (it uses server_url + token).
    self.assertEqual(LlmConfig(provider="stencil-server").base_url, "")

  def test_from_env_reads_stencil_llm_keys(self) -> None:
    cfg = LlmConfig.from_env(
      {
        "STENCIL_LLM_PROVIDER": "openai-compat",
        "STENCIL_LLM_BASE_URL": "http://lmstudio:9999/v1",
        "STENCIL_LLM_MODEL": "qwen2-vl",
        "STENCIL_LLM_API_KEY": "sk-xyz",
        "STENCIL_LLM_SERVER_URL": "https://stencil.example.com:8090",
      }
    )
    self.assertEqual(cfg.provider, "openai-compat")
    self.assertEqual(cfg.base_url, "http://lmstudio:9999/v1")
    self.assertEqual(cfg.model, "qwen2-vl")
    self.assertEqual(cfg.api_key, "sk-xyz")
    self.assertEqual(cfg.server_url, "https://stencil.example.com:8090")

  def test_from_env_empty_falls_back_to_defaults(self) -> None:
    cfg = LlmConfig.from_env({})
    self.assertEqual(cfg.provider, "ollama")
    self.assertEqual(cfg.base_url, "http://localhost:11434")
    self.assertEqual(cfg.model, "")

  def test_unknown_provider_raises(self) -> None:
    with self.assertRaises(ValueError):
      LlmConfig(provider="anthropic-direct")

  def test_provider_is_trimmed_and_lowercased(self) -> None:
    self.assertEqual(LlmConfig(provider=" Ollama ").provider, "ollama")

  def test_set_provider_refills_default_url(self) -> None:
    cfg = LlmConfig()  # ollama default URL
    cfg.set_provider("openai-compat")
    self.assertEqual(cfg.base_url, "http://localhost:1234/v1")
    cfg.set_provider("ollama")
    self.assertEqual(cfg.base_url, "http://localhost:11434")

  def test_set_provider_keeps_pinned_url(self) -> None:
    cfg = LlmConfig()
    cfg.set_base_url("http://box:9999/v1")  # a user-pinned override
    cfg.set_provider("openai-compat")
    self.assertEqual(cfg.base_url, "http://box:9999/v1")
    # An explicit keep_url overrides the pinned state either way.
    cfg.set_provider("ollama", keep_url=False)
    self.assertEqual(cfg.base_url, "http://localhost:11434")

  def test_set_provider_rejects_unknown_and_changes_nothing(self) -> None:
    cfg = LlmConfig()
    with self.assertRaises(ValueError):
      cfg.set_provider("anthropic-direct")
    self.assertEqual(cfg.provider, "ollama")
    self.assertEqual(cfg.base_url, "http://localhost:11434")


class VariantSlugTest(unittest.TestCase):
  def test_slug_sanitizes_labels(self) -> None:
    self.assertEqual(variant_slug("B&W One!"), "b-w-one")
    self.assertEqual(variant_slug("  plain  "), "plain")
    self.assertEqual(variant_slug(""), "variant")

  def test_slugs_dedupe_collisions(self) -> None:
    # Same-slug labels get "-2"/"-3"… suffixes (like the Zig CLI / mcp) so the
    # rendered variant files can't overwrite each other.
    variants = [
      Variant(label="Rotated!"),
      Variant(label="rotated"),
      Variant(label="ROTATED"),
      Variant(label="other"),
    ]
    self.assertEqual(
      variant_slugs(variants), ["rotated", "rotated-2", "rotated-3", "other"]
    )
