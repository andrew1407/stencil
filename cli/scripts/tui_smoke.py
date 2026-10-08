#!/usr/bin/env python3
"""Opt-in smoke check for the full-screen console TUI (`stencil --console-full-screen`).

The TUI only runs on a real terminal, so this drives it through a pseudo-terminal sized past the
plain-editor fallback and asserts on the raw escape stream. Stdlib only, macOS and Linux; NOT in
CI — the deferred single-click and the animations are clock-paced, so it is a manual check.

Usage:
    python3 cli/scripts/tui_smoke.py [path/to/stencil]
Default binary: cli/zig-out/bin/stencil (build it first with `zig build`).
"""
import os, re, sys, pty, fcntl, termios, struct, select, time

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_BIN = os.path.join(HERE, "..", "zig-out", "bin", "stencil")
BIN = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else DEFAULT_BIN)
ROWS, COLS = 40, 120

if not os.path.exists(BIN):
    sys.exit(f"binary not found: {BIN}\nbuild it first: (cd cli && zig build)")

pid, master = pty.fork()  # child is already setsid + owns the slave as its controlling tty
if pid == 0:
    os.execv(BIN, [BIN, "--console-full-screen", "/tmp/tui_smoke_out.png"])
    os._exit(127)

# Size the terminal so screen.start() measures a usable window instead of falling back.
fcntl.ioctl(master, termios.TIOCSWINSZ, struct.pack("HHHH", ROWS, COLS, 0, 0))

captured = bytearray()

def pump(seconds):
    end = time.time() + seconds
    while time.time() < end:
        r, _, _ = select.select([master], [], [], 0.1)
        if r:
            try:
                data = os.read(master, 65536)
            except OSError:
                return
            if not data:
                return
            captured.extend(data)

def send(b):
    try:
        os.write(master, b if isinstance(b, bytes) else b.encode())
    except OSError:
        pass  # child gone; whatever was captured is still asserted below

pump(0.6)                                  # initial paint
send("/help\r"); pump(0.5)                 # a command -> echoed into scrollback + its output
send("\x1b[<0;6;2M"); send("\x1b[<0;6;2m") # synthetic left click on the pinned logo (row 2, col 6)
pump(1.3)                                  # let the deferred single-click fire (cycle theme) + animate
send("/exit\r"); pump(0.5)

try:
    os.waitpid(pid, os.WNOHANG)
except OSError:
    pass

out = bytes(captured)

def check(seq, label):
    ok = (seq if isinstance(seq, bytes) else seq.encode()) in out
    print(f"  [{'PASS' if ok else 'FAIL'}] {label}")
    return ok

def check_press_leaves_wordmark():
    """The press animates the icon only: its rows stop at the icon's width, so the wordmark beside it
    is never repainted."""
    start = out.find(b"\x1b[1;1H\x1b[0m")  # the press frame opens by blanking row 1
    frame = out[start:start + 1200] if start >= 0 else b""
    rows = re.split(rb"(?=\x1b\[\d+;1H)", frame)[1:11]
    ok = len(rows) == 10 and not any(b"S T E N C I L" in r for r in rows)
    print(f"  [{'PASS' if ok else 'FAIL'}] the press repaints the icon only, not the wordmark")
    return ok

def check_wipe_seam():
    """A row carrying BOTH accents at once = the recolour is sweeping, not flipping at once.
    True of the text seam mid-travel and of the icon rows mid-turn alike."""
    rows = re.split(rb"(?=\x1b\[\d+;1H)", out)
    ok = any(b"38;2;124;58;237m" in r and b"38;2;236;72;153m" in r for r in rows)
    print(f"  [{'PASS' if ok else 'FAIL'}] recolour swept in (a row held both accents)")
    return ok

print(f"captured {len(out)} bytes")
results = [
    check("\x1b[?1049h",         "entered alternate screen"),
    check("\x1b[?1002h",         "enabled SGR drag mouse reporting"),
    check("\x1b[?7l",            "disabled autowrap (pin the header)"),
    check("━",                   "drew the accent rule (heavy line)"),
    check("upload",              "help text rendered into scrollback"),
    check("38;2;124;58;237",     "violet accent painted before the click"),
    check("38;2;236;72;153",     "pink accent painted after the logo click (theme cycled)"),
    # The pressed logo: the whole icon shrinks, so its rounded top row starts two columns
    # further in than the full one's — a row no other frame draws.
    check("    \x1b[38;2;124;58;237m╭", "logo flashed its smaller pressed frame on the click"),
    check_press_leaves_wordmark(),
    check_wipe_seam(),
    check("\x1b[?1049l",         "restored the primary screen on exit"),
    check("\x1b[?1002l",         "disabled mouse on exit"),
    check("\x1b[?7h",            "re-enabled autowrap on exit"),
]
print("RESULT:", "ALL PASS" if all(results) else "SOME FAILED")
sys.exit(0 if all(results) else 1)
