"""Drift tests pinning pystencil's mirrored canonical data to the source of truth.

The Phase-4 audit found no canonical-table duplication in pystencil: page
formats/sizes come from the native core over ctypes, and no accent, color-name,
or icon tables exist here. What IS hand-mirrored are a few scalar constants —
asserted equal to ``browser/js/config/constants.json`` so any upstream change
fails loudly while the package stays relocatable. The checked-in ``_data/``
copies are the LLM system-prompt asset (Phase 5) and the LLM providers asset
(Phase 7), byte-pinned below against their ``browser/js/config/llm/`` originals.
"""

from __future__ import annotations

import json
import sys
import unittest
from pathlib import Path

# Make the package importable when running `python3 -m unittest` from pystencil/.
_PKG_ROOT = Path(__file__).resolve().parent.parent
if str(_PKG_ROOT) not in sys.path:
    sys.path.insert(0, str(_PKG_ROOT))

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


if __name__ == "__main__":
    unittest.main()
