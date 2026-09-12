"""Import-direction lint for the ``pystencil`` package.

``.claude/rules/architecture.md`` orders the layers ``_native``/``core`` → ``image``/
``codecs``/``layout`` → ``editor`` → ``llm``/``server``/``sitesource`` → ``cli``; a module
may import from every layer to its left and none to its right. The other ``_``-prefixed
helpers sit with ``_native`` at the bottom. Today's crossings are a frozen allowance:
shrink it, never add to it. The package root (``__init__``/``__main__``) is the facade
and is not scanned.
"""

from __future__ import annotations

import ast
import tempfile
import unittest
from pathlib import Path

from tests import _PKG_ROOT

PACKAGE = Path(_PKG_ROOT) / "pystencil"

LAYERS = (
    ("_native", "core"),
    ("image", "codecs", "layout"),
    ("editor",),
    ("llm", "server", "sitesource"),
    ("cli",),
)
RANK = {name: i for i, names in enumerate(LAYERS) for name in names}

ALLOWANCE = {
    "editor/assistant.py → llm": "Editor.ask() is a lazy in-function delegate to pystencil.llm",
    "editor/project.py → llm": "MAX_HISTORY / chat_display_text are read by the project file",
}


def _rank(module):
    if module.startswith("_"):
        return 0
    return RANK.get(module)


def _owner(rel):
    head = rel.split("/")[0]
    name = head[:-3] if head.endswith(".py") else head
    return None if name in ("__init__", "__main__") else name


def _targets(node, package_parts):
    """Top-level package modules one import statement reaches, or [] for foreign imports."""
    if isinstance(node, ast.Import):
        return [a.name.split(".")[1] for a in node.names
                if a.name.split(".")[0] == "pystencil" and "." in a.name]
    if node.level == 0:
        parts = (node.module or "").split(".")
        if parts[0] != "pystencil":
            return []
        return [parts[1]] if len(parts) > 1 else [a.name for a in node.names]
    base = package_parts[: len(package_parts) - (node.level - 1)]
    full = base + ((node.module or "").split(".") if node.module else [])
    return [full[0]] if full else [a.name for a in node.names]


def scan(package):
    """Every cross-module import as (file, from_module, to_module), file-sorted."""
    edges = []
    for path in sorted(package.rglob("*.py")):
        rel = path.relative_to(package).as_posix()
        owner = _owner(rel)
        if owner is None:
            continue
        parts = rel[:-3].split("/")
        package_parts = parts[:-1]
        tree = ast.parse(path.read_text(encoding="utf-8"), filename=str(path))
        for node in ast.walk(tree):
            if not isinstance(node, (ast.Import, ast.ImportFrom)):
                continue
            for target in _targets(node, package_parts):
                if target != owner and (rel, owner, target) not in edges:
                    edges.append((rel, owner, target))
    return edges


def violations(edges):
    """(rightward edges as "file → module", modules with no layer)."""
    rightward, unranked = [], set()
    for rel, owner, target in edges:
        a, b = _rank(owner), _rank(target)
        if a is None:
            unranked.add(owner)
        if b is None:
            unranked.add(target)
        if a is not None and b is not None and b > a:
            rightward.append("%s → %s" % (rel, target))
    return rightward, unranked


class LayerBoundaryTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.edges = scan(PACKAGE)
        cls.rightward, cls.unranked = violations(cls.edges)

    def test_the_scan_saw_the_whole_tree(self):
        self.assertGreater(len(self.edges), 40)
        pairs = {(a, b) for _, a, b in self.edges}
        self.assertIn(("cli", "editor"), pairs)
        self.assertIn(("editor", "image"), pairs)
        self.assertIn(("core", "_native"), pairs)

    def test_every_module_has_a_layer(self):
        self.assertEqual(self.unranked, set())

    def test_no_module_imports_from_a_layer_to_its_right(self):
        unlisted = [e for e in self.rightward if e not in ALLOWANCE]
        self.assertEqual(unlisted, [], "a new right-pointing import; move the code left instead")

    def test_every_allowance_is_still_needed(self):
        stale = [e for e in ALLOWANCE if e not in self.rightward]
        self.assertEqual(stale, [], "the violation is gone; delete its allowance")

    def test_an_injected_right_pointing_import_is_caught(self):
        files = {
            "__init__.py": "from .cli import main\n",
            "core.py": "from . import _native\nfrom .editor import Editor\n",
            "_native.py": "import os\n",
            "image.py": "from pystencil.core import Core\nimport pystencil.codecs\n",
            "codecs/__init__.py": "from ._net import x\n",
            "codecs/png.py": "from .. import layout\nfrom ..llm.plan import Plan\n",
            "editor/__init__.py": "from ..image import Image\n",
            "llm/__init__.py": "from .plan import Plan\n",
            "llm/plan.py": "from ..editor import Editor\n",
            "cli/__init__.py": "def main():\n    from ..llm import Plan\n",
            "_net.py": "from . import _parallel\n",
            "extra.py": "from .cli import main\n",
        }
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp) / "pystencil"
            for rel, body in files.items():
                (root / rel).parent.mkdir(parents=True, exist_ok=True)
                (root / rel).write_text(body, encoding="utf-8")
            rightward, unranked = violations(scan(root))
        self.assertEqual(rightward, ["codecs/png.py → llm", "core.py → editor"])
        self.assertEqual(unranked, {"extra"})


if __name__ == "__main__":
    unittest.main()
