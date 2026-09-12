"""Drift tests pinning pystencil's mirrored canonical data to the source of truth.

pystencil duplicates no canonical table: page formats/sizes come from the native
core over ctypes, and no accent, color-name or icon tables exist here. What IS
hand-mirrored are a few scalar constants — asserted equal to
``browser/js/config/constants.json`` so any upstream change fails loudly while the
package stays relocatable. The checked-in ``_data/`` copies are the LLM
system-prompt and providers assets, byte-pinned below against their
``browser/js/config/llm/`` originals.
"""

from __future__ import annotations

import json
import unittest
from pathlib import Path

from tests import _PKG_ROOT

from pystencil import layout
from pystencil.editor import _A4_FALLBACK

# Canonical config root, __file__-relative: pystencil/tests → repo root → browser.
_CONSTANTS = (
    Path(__file__).resolve().parent.parent.parent / "browser" / "js" / "config" / "constants.json"
)


class CanonicalConstantsDriftTests(unittest.TestCase):
    def setUp(self):
        with open(_CONSTANTS, encoding="utf-8") as fh:
            self.canon = json.load(fh)

    def test_layout_line_defaults_match_default_visuals(self):
        """layout.py per-line defaults == canonical DEFAULT_VISUALS.

        locked=False / fillColor="transparent" have no canonical counterpart
        (DEFAULT_VISUALS.defaultFillColor is the UI picker default, not the
        tolerant-parse default), so only the four shared fields are pinned.
        """
        visuals = self.canon["DEFAULT_VISUALS"]
        manifest = [
            ("DEFAULT_COLOR", layout.DEFAULT_COLOR, visuals["color"]),
            ("DEFAULT_THICKNESS", layout.DEFAULT_THICKNESS, visuals["thickness"]),
            ("DEFAULT_POINT_SIZE", layout.DEFAULT_POINT_SIZE, visuals["pointSize"]),
            ("DEFAULT_STYLE", layout.DEFAULT_STYLE, visuals["style"]),
        ]
        for name, ours, canonical in manifest:
            with self.subTest(constant=name):
                self.assertEqual(ours, canonical)

    def test_editor_a4_fallback_matches_page_sizes(self):
        """editor.py's A4 fallback (cm) == canonical PAGE_SIZES.A4."""
        a4 = self.canon["PAGE_SIZES"]["A4"]
        self.assertEqual(_A4_FALLBACK, (a4["width"], a4["height"]))


# Canonical LLM system-prompt asset + the checked-in copy llm.py loads at import.
_CANON_PROMPT = _CONSTANTS.parent / "llm" / "systemPrompt.json"
_DATA_PROMPT = _PKG_ROOT / "pystencil" / "_data" / "systemPrompt.json"


class SystemPromptAssetDriftTests(unittest.TestCase):
    def test_data_copy_is_byte_identical_to_canonical(self):
        """pystencil/_data/systemPrompt.json == browser's canonical asset, byte-for-byte."""
        self.assertEqual(_DATA_PROMPT.read_bytes(), _CANON_PROMPT.read_bytes())

    def test_head_and_tail_shape(self):
        """Length + boundary pins on the loaded asset (llm-contract.md §4)."""
        asset = json.loads(_DATA_PROMPT.read_text(encoding="utf-8"))
        head, tail = asset["head"], asset["tail"]
        self.assertEqual(len(head.encode()), 1197)
        self.assertEqual(len(tail.encode()), 4930)
        self.assertTrue(head.startswith("You are the AI assistant inside Stencil"))
        self.assertTrue(head.endswith("no free-angle rotation):\n"))
        self.assertTrue(tail.startswith("\n\nWhen a choice is genuinely"))
        self.assertTrue(tail.endswith("follow."))

    def test_prompt_assembles_from_the_asset(self):
        """LLM_SYSTEM_PROMPT = asset head + generated bullets + tail (console-ask spliced)."""
        from pystencil.llm import LLM_SYSTEM_PROMPT

        asset = json.loads(_DATA_PROMPT.read_text(encoding="utf-8"))
        self.assertTrue(LLM_SYSTEM_PROMPT.startswith(asset["head"]))
        shared = asset["tail"][asset["tail"].index("\n\nOutlining (") :]
        self.assertTrue(LLM_SYSTEM_PROMPT.endswith(shared))


# Canonical LLM providers asset + the checked-in copy llm.py/server.py load at import.
_CANON_PROVIDERS = _CONSTANTS.parent / "llm" / "providers.json"
_DATA_PROVIDERS = _PKG_ROOT / "pystencil" / "_data" / "providers.json"


class ProvidersAssetDriftTests(unittest.TestCase):
    def test_data_copy_is_byte_identical_to_canonical(self):
        """pystencil/_data/providers.json == browser's canonical asset, byte-for-byte."""
        self.assertEqual(_DATA_PROVIDERS.read_bytes(), _CANON_PROVIDERS.read_bytes())

    def test_constants_derive_from_the_asset(self):
        """PROVIDERS / DEFAULT_BASE_URLS / _LLM_TIMEOUT come from the asset."""
        from pystencil.llm import DEFAULT_BASE_URLS, PROVIDERS
        from pystencil.server import _LLM_TIMEOUT

        asset = json.loads(_DATA_PROVIDERS.read_text(encoding="utf-8"))
        self.assertEqual(PROVIDERS, tuple(asset["providers"]))
        self.assertEqual(
            DEFAULT_BASE_URLS,
            {
                name: p["defaultBaseUrl"]
                for name, p in asset["providers"].items()
                if p["defaultBaseUrl"]
            },
        )
        self.assertEqual(_LLM_TIMEOUT, float(asset["timeouts"]["chatSeconds"]))


# Canonical LLM op registry + the checked-in copy _opschema.py loads on first use.
_CANON_REGISTRY = _CONSTANTS.parent / "llm" / "opRegistry.json"
_DATA_REGISTRY = _PKG_ROOT / "pystencil" / "_data" / "opRegistry.json"


class OpRegistryAssetDriftTests(unittest.TestCase):
    def test_data_copy_is_byte_identical_to_canonical(self):
        """pystencil/_data/opRegistry.json == browser's canonical registry, byte-for-byte."""
        self.assertEqual(_DATA_REGISTRY.read_bytes(), _CANON_REGISTRY.read_bytes())

    def test_validator_tables_derive_from_the_asset(self):
        """Limits, membership, flags and the forbidden list come from the asset."""
        from pystencil.llm import FORBIDDEN_OPS, MAX_ACTIONS, MAX_ASK_OPTIONS, OP_REGISTRY, SCHEMA

        asset = json.loads(_DATA_REGISTRY.read_text(encoding="utf-8"))
        self.assertEqual(asset["$meta"]["schemaVersion"], 2)
        self.assertEqual(asset["$meta"]["surfaceProfiles"]["pystencil"], SCHEMA.profile)
        self.assertEqual(MAX_ACTIONS, asset["limits"]["MAX_ACTIONS"])
        self.assertEqual(MAX_ASK_OPTIONS, asset["limits"]["ask"]["maxOptions"])
        self.assertEqual(FORBIDDEN_OPS, tuple(asset["forbidden"]["perSurface"]["pystencil"]))
        mine = [
            e for e in asset["ops"]
            if SCHEMA.profile in e["profiles"]
            and (not e.get("surfaces") or "pystencil" in e["surfaces"])
        ]
        self.assertEqual(set(OP_REGISTRY), {e["name"] for e in mine})
        for e in mine:
            keys = (e.get("surfaceKeys") or {}).get("pystencil") or e["keys"]
            self.assertEqual(OP_REGISTRY[e["name"]].fields, frozenset({"op", *keys}), e["name"])

    def test_prompt_bullets_come_from_the_asset(self):
        """Every registered op's bullet IS the asset's — no hand-copied prose here.

        A bullet shared by two ops (undo/redo, connect/disconnect) sits on the first
        entry; the partner's asset bullet is null and registers as empty.
        """
        from pystencil.llm import OP_REGISTRY, SCHEMA

        asset = json.loads(_DATA_REGISTRY.read_text(encoding="utf-8"))
        seen = 0
        for e in asset["ops"]:
            name = e["name"]
            if SCHEMA.profile not in e["profiles"] or name not in OP_REGISTRY:
                continue  # a name can repeat across profiles (extension's own "filter")
            variants = e.get("bulletVariants") or {}
            variant = variants.get("pystencil")
            if variant is None:
                variant = variants.get(SCHEMA.profile)
            expected = variant if isinstance(variant, str) else e.get("bullet")
            self.assertEqual(OP_REGISTRY[name].bullet, expected or "", name)
            seen += 1
        self.assertEqual(seen, len(OP_REGISTRY))

    def test_assembled_prompts_carry_the_asset_bullets(self):
        """Each op's asset bullet appears verbatim in the block that carries it."""
        from pystencil.llm import CONSOLE_SYSTEM_PROMPT, LLM_SYSTEM_PROMPT, OP_REGISTRY

        for name, spec in OP_REGISTRY.items():
            if not spec.bullet:
                continue
            with self.subTest(op=name):
                self.assertIn(spec.bullet, CONSOLE_SYSTEM_PROMPT)
                self.assertEqual(spec.scope == "core", spec.bullet in LLM_SYSTEM_PROMPT)

    def test_assembled_prompt_lengths(self):
        """Byte pins on the two assembled prompts — a canonical bullet edit fails here.

        The console prompt is the LLM one with the §10 block spliced at the shared ask
        anchor, so it stays the longer of the two.
        """
        from pystencil.llm import (
            CONSOLE_SETTINGS_PROMPT,
            CONSOLE_SPLICE_ANCHOR,
            CONSOLE_SYSTEM_PROMPT,
            LLM_SYSTEM_PROMPT,
        )

        self.assertEqual(len(LLM_SYSTEM_PROMPT.encode()), 8678)
        self.assertEqual(len(CONSOLE_SETTINGS_PROMPT.encode()), 1532)
        self.assertEqual(len(CONSOLE_SYSTEM_PROMPT.encode()), 10211)
        self.assertIn("\n" + CONSOLE_SETTINGS_PROMPT + CONSOLE_SPLICE_ANCHOR, CONSOLE_SYSTEM_PROMPT)


if __name__ == "__main__":
    unittest.main()
