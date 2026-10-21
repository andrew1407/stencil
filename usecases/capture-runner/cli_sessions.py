#!/usr/bin/env python3
"""Records the CLI or pystencil in a pseudo-terminal, as a user sees it: a bare shell on a neutral
root types each scene's lines into the real binaries, and what they print is replayed through
terminal_renderer into HTML stills and timed GIF frames for cliTerminal.mjs to photograph.
Run by cliTerminal.mjs:  python3 cli_sessions.py <app> --stub <url> --server <url> [--only a,b]
"""

from __future__ import annotations

import argparse
import fcntl
import json
import os
import pty
import re
import select
import shutil
import signal
import struct
import subprocess
import termios
import time

from terminal_renderer import TerminalEmulator

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
SCRATCH = os.environ.get("STENCIL_CAPTURE_SCRATCH", os.path.join(HERE, ".out"))
SYNC = re.compile(rb"\x1b\[\?2026([hl])")
# A session token for the collaboration server, handed over in the environment, never argv.
TOKEN_ENV = "STENCIL_DOCS_SESSION_TOKEN"
# Nothing a scene runs may open a window: the platform openers only note what they were given.
OPENER = "#!/bin/sh\nprintf '%%s\\n' \"$@\" >> '%s'\n"


def prepare(spec: dict, root: str) -> None:
  """The neutral root: bin/ (the binaries, the stub openers, ffmpeg), ws/ (the scene files)."""
  shutil.rmtree(root, ignore_errors=True)
  bin_dir, ws = os.path.join(root, "bin"), os.path.join(root, "ws")
  for d in (bin_dir, os.path.join(ws, "out")): os.makedirs(d)
  for name, rel in spec.get("bin", dict()).items(): shutil.copy2(os.path.join(REPO, rel), os.path.join(bin_dir, name))
  for opener in ("open", "xdg-open"):
    with open(os.path.join(bin_dir, opener), "w") as dst: dst.write(OPENER % os.path.join(root, "opened.txt"))
  for tool in ("ffmpeg", "ffprobe"):
    found = shutil.which(tool)
    if found: os.symlink(found, os.path.join(bin_dir, tool))
  for name in os.listdir(bin_dir):
    if not os.path.islink(os.path.join(bin_dir, name)): os.chmod(os.path.join(bin_dir, name), 0o755)
  for name, rel in spec.get("files", dict()).items():
    os.makedirs(os.path.dirname(os.path.join(ws, name)), exist_ok=True)
    shutil.copy2(os.path.join(REPO, rel), os.path.join(ws, name))
  for name, rel in spec.get("links", dict()).items():   # a package importable from the neutral root
    os.makedirs(os.path.dirname(os.path.join(root, name)), exist_ok=True)
    os.symlink(os.path.join(REPO, rel), os.path.join(root, name))
  for name, lines in spec.get("texts", dict()).items():
    with open(os.path.join(ws, name), "w") as dst: dst.write("\n".join(lines) + "\n")
  if spec.get("video"):   # the clip lib/sampleMedia.mjs makes: a moving pattern with a timecode
    subprocess.run(["ffmpeg", "-y", "-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i",
                    "testsrc2=size=480x270:rate=12:duration=4", "-pix_fmt", "yuv420p", "-c:v", "libx264",
                    os.path.join(ws, spec["video"])], check=True)


class Session:
  """One shell in a pseudo-terminal, its output fed live into a TerminalEmulator."""

  def __init__(self, root: str, env: dict, rows: int, cols: int, title: str):
    self.term, self.title, self.sync = TerminalEmulator(rows, cols), title, False
    self.quiet_since, self.film, self.pointer = time.time(), None, None
    self.pid, self.fd = pty.fork()
    if self.pid == 0:
      os.chdir(os.path.join(root, "ws"))
      os.execve("/bin/bash", ["bash", "--norc", "--noprofile", "-i"], env)
    fcntl.ioctl(self.fd, termios.TIOCSWINSZ, struct.pack("HHHH", rows, cols, 0, 0))

  def text(self) -> str:
    return "\n".join("".join(c.ch for c in line).rstrip() for line in self.term.grid)

  def pump(self, seconds: float) -> None:
    end = time.time() + seconds
    while time.time() < end:
      ready, _, _ = select.select([self.fd], list(), list(), min(0.02, max(0.0, end - time.time())))
      if ready:
        try: data = os.read(self.fd, 65536)
        except OSError: data = b""
        if not data: return
        self.term.feed(data)
        for m in SYNC.finditer(data): self.sync = m.group(1) == b"h"
        self.quiet_since = time.time()
      self.__snap()

  def until(self, pattern: str, timeout: float) -> re.Match:
    end = time.time() + timeout
    while time.time() < end:
      found = re.search(pattern, self.text())
      if found: return found
      self.pump(0.05)
    raise TimeoutError("waited %.0fs for /%s/; the screen reads:\n%s" % (timeout, pattern, self.text()))

  def idle(self, quiet: float, timeout: float = 30.0) -> None:
    end = time.time() + timeout
    while time.time() < end and (time.time() - self.quiet_since < quiet or self.sync): self.pump(0.05)

  def type(self, text: str, delay: float) -> None:
    for ch in text:
      os.write(self.fd, ch.encode())
      self.pump(delay)

  # SGR mouse (modes 1002/1006): a press and a release on one cell, both 1-based. Frames show a
  # pointer on that cell for a moment, since a terminal draws none.
  def click(self, row: int, col: int) -> None:
    self.pointer = (row, col, time.time() + 0.8)
    for end in "Mm":
      os.write(self.fd, ("\x1b[<0;%d;%d%s" % (col, row, end)).encode())
      self.pump(0.08)

  def keep_rows(self, fit: str) -> list:
    rows = self.term.rows
    empty = ["".join(c.ch for c in self.term.grid[r]).strip() == "" if r < len(self.term.grid) else True
             for r in range(rows)]
    if fit == "rows":   # the output and the prompt under it, no blank screen below
      last = max([r for r in range(rows) if not empty[r]] or [0])
      return list(range(last + 1))
    if fit == "band":   # the longest run of blank rows collapses to one
      best, start = (0, 0), None
      for r in range(rows + 1):
        if r < rows and empty[r]: start = r if start is None else start
        elif start is not None:
          best, start = max(best, (r - start, start)), None
      return [r for r in range(rows) if not (best[1] < r < best[1] + best[0])]
    return list(range(rows))

  def shot(self, path: str, fit: str) -> None:
    with open(path, "w") as dst: dst.write(self.term.to_html(self.title, self.keep_rows(fit)))

  # Filming: a frame whenever the screen changed, at most every `every` s, never mid-redraw.
  def start_film(self, folder: str, every: float) -> None:
    shutil.rmtree(folder, ignore_errors=True)
    os.makedirs(folder)
    self.film = dict(folder=folder, every=every, last=0.0, html="", frames=list())

  def __snap(self) -> None:
    film, now = self.film, time.time()
    if not film or self.sync or now - film["last"] < film["every"]: return
    pointer = self.pointer[:2] if self.pointer and now < self.pointer[2] else None
    film["last"], html = now, self.term.to_html(self.title)
    if (html, pointer) == film["html"]: return
    name = "frame-%05d.html" % (len(film["frames"]) + 1)
    with open(os.path.join(film["folder"], name), "w") as dst: dst.write(html)
    film["html"] = (html, pointer)
    film["frames"].append(dict(file=name, at=now, pointer=pointer))

  def cut(self) -> None:
    film, self.film = self.film, None
    with open(os.path.join(film["folder"], "frames.json"), "w") as dst:
      json.dump(dict(frames=film["frames"], end=time.time()), dst)

  def close(self) -> None:
    os.close(self.fd)
    try: os.kill(self.pid, signal.SIGHUP)
    except ProcessLookupError: pass
    os.waitpid(self.pid, 0)


def shell_env(spec: dict, scene: dict, root: str, values: dict) -> dict:
  env = dict(PATH=os.path.join(root, "bin") + ":/usr/bin:/bin", HOME=root, TERM="xterm-256color",
             LANG="en_US.UTF-8", PS1="$ ", BASH_SILENCE_DEPRECATION_WARNING="1")
  for key, value in {**spec.get("env", dict()), **scene.get("env", dict())}.items():
    env[key] = value.format(**values)
  return env


def play(scene: dict, spec: dict, root: str, out: str, values: dict) -> None:
  rows, cols = scene.get("rows", spec["rows"]), scene.get("cols", spec["cols"])
  session = Session(root, shell_env(spec, scene, root, values), rows, cols, scene.get("title", spec["title"]))
  delay, quiet = spec["typeDelayMs"] / 1000, spec["quietMs"] / 1000
  try:
    session.until(r"\$", 10)
    for step in scene["steps"]: run_step(session, step, out, values, delay, quiet)
  finally:
    session.close()


def run_step(session: Session, step: dict, out: str, values: dict, delay: float, quiet: float) -> None:
  timeout = step.get("timeout", 30)
  if "line" in step:
    session.type(step["line"].format(**values) + "\r", delay)
  if "keys" in step:
    session.type(step["keys"], delay)
  if "click" in step:
    session.click(*step["click"])
  if "until" in step:
    session.until(step["until"], timeout)
  if "pauseMs" in step:
    session.pump(step["pauseMs"] / 1000)
  else:
    session.idle(step.get("quietMs", quiet * 1000) / 1000, timeout)
  if "clip" in step:
    session.start_film(os.path.join(out, step["clip"]), step.get("everyMs", 100) / 1000)
  if step.get("cut"):
    session.cut()
  if "shot" in step:
    session.shot(os.path.join(out, step["shot"] + ".html"), step.get("fit", "screen"))
  if "keep" in step:
    src, name = step["keep"]
    shutil.copy2(os.path.join(values["root"], "ws", src), os.path.join(out, name + ".png"))


def main() -> None:
  parser = argparse.ArgumentParser()
  parser.add_argument("app")
  parser.add_argument("--stub", default="")
  parser.add_argument("--server", default="")
  parser.add_argument("--only", default="")
  args = parser.parse_args()
  with open(os.path.join(HERE, "config", args.app + ".json")) as src: spec = json.load(src)["sessions"]
  root, out = spec["root"], os.path.join(SCRATCH, args.app + "-sessions")
  prepare(spec, root)
  os.makedirs(out, exist_ok=True)
  with open(os.path.join(HERE, "config", "shared.json")) as src: shared = json.load(src)
  values = dict(stub=args.stub, server=args.server, repo=REPO, root=root, token=os.environ.get(TOKEN_ENV, ""),
                iconUrl=shared["urls"]["raw"] + shared["urls"]["botIcon"], prompt=shared["prompts"]["stub"])
  only = [o for o in args.only.split(",") if o]
  for scene in spec["scenes"]:
    if only and not any(scene["name"].startswith(o) for o in only): continue
    print("  %s" % scene["name"], flush=True)
    play(scene, spec, root, out, values)


if __name__ == "__main__":
  main()
