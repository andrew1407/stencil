#!/usr/bin/env python3
"""Regenerates every use-case image except the bot's (that one needs a Telegram login; see
README.md). Runs the apps one after another: several at once fight over the machine.
Run:  python3 usecases/capture-runner/main.py
"""

from __future__ import annotations

import shutil
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]

# Order matters: the two terminal runners photograph the same VS Code debug port, and the
# desktop and extension runners each want the machine's screen to themselves.
RUNNERS = (
    "browser.mjs",
    "cli_listings.py",
    "cliRender.mjs",
    "cliTerminal.mjs",
    "browserExtension.mjs",
    "desktop.mjs",
    "vscodeExtension.mjs",
)


def interpreter(name: str) -> str:
    """sys.executable, not a `python3` off PATH: a .py runner shares this interpreter."""
    if name.endswith(".py"):
        return sys.executable
    node = shutil.which("node")
    if node is None:
        raise SystemExit("node is not on PATH — see README.md for the prerequisites")
    return node


def changed_images() -> list[str]:
    out = subprocess.run(
        ["git", "status", "--porcelain", "usecases"],
        cwd=REPO, capture_output=True, text=True, check=True,
    ).stdout
    return [line for line in out.splitlines() if line.endswith((".png", ".gif"))]


def main() -> int:
    for name in RUNNERS:
        code = subprocess.run([interpreter(name), str(HERE / name)], cwd=REPO).returncode
        if code != 0:
            print(f"{name} failed (exit {code})", file=sys.stderr)
            return code

    print()
    print("changed images:")
    for line in changed_images() or ["  none"]:
        print(line)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
