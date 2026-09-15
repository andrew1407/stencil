#!/usr/bin/env python3
"""Renders the CLI's pinned terminal goldens (cli/tests/pins/*.color.txt) to HTML with
terminal_renderer, for cliRender.mjs to photograph. The full-screen console itself is
captured in a real terminal by cliTerminal.mjs.
Run:  python3 usecases/capture-runner/cli_listings.py
"""

from __future__ import annotations

import json
import os
import shutil
from dataclasses import dataclass

from terminal_renderer import render

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
PINS = os.path.join(REPO, "cli", "tests", "pins")
CONFIG = os.path.join(HERE, "config", "cli.json")
SCRATCH = os.path.join(os.environ.get("STENCIL_CAPTURE_SCRATCH", os.path.join(HERE, ".out")), "cli")


@dataclass
class Golden:
  """One SGR-coloured pin, rendered as it prints: config/cli.json `goldens` entry."""
  name: str
  pin: str
  cols: int
  title: str

  @staticmethod
  def of(name: str, spec: dict) -> Golden:
    return Golden(name, spec["pin"], spec["cols"], spec["title"])

  @property
  def source(self) -> str:
    return os.path.join(PINS, self.pin)


def load_goldens() -> tuple[Golden, ...]:
  with open(CONFIG) as src: config = json.load(src)
  return tuple(Golden.of(name, spec) for name, spec in config["goldens"].items())


def render_goldens(goldens: tuple[Golden, ...]) -> tuple[str, ...]:
  shutil.rmtree(SCRATCH, ignore_errors=True)
  os.makedirs(SCRATCH)
  written = list()
  for golden in goldens:
    written.append(render(golden.source, os.path.join(SCRATCH, golden.name + ".html"),
                          None, golden.cols, golden.title))
    print("  %s.html" % golden.name)
  return tuple(written)


if __name__ == "__main__":
  render_goldens(load_goldens())
