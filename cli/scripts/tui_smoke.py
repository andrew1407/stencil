#!/usr/bin/env python3
"""Opt-in smoke check for the full-screen console TUI (`stencil --console-full-screen`).

The interactive TUI only runs on a real terminal — piped stdin takes the plain line-oriented
path, and `zig build test` never enters raw mode — so this can't live in the Zig test suite.
Instead it allocates a pseudo-terminal, sizes it tall enough that the screen doesn't fall back
to the plain editor, feeds keystrokes plus a synthetic SGR mouse click, and asserts on the raw
escape stream the app emits (alt-screen, pinned header, mouse reporting, the accent rule, a
theme cycle from the logo click, and a clean teardown).

Stdlib only; runs on macOS and Linux. NOT wired into CI — it is timing-dependent (the deferred
single-click needs a real wait, and the wordmark flourish is clock-paced), so treat it as a
manual "does the TUI still paint" check, not a gate.

Usage:
    python3 cli/scripts/tui_smoke.py [path/to/stencil]
Default binary: cli/zig-out/bin/stencil (build it first with `zig build`).
"""
import os, sys, pty, fcntl, termios, struct, select, time

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

print(f"captured {len(out)} bytes")
results = [
    check("\x1b[?1049h",         "entered alternate screen"),
    check("\x1b[?1002h",         "enabled SGR drag mouse reporting"),
    check("\x1b[?7l",            "disabled autowrap (pin the header)"),
    check("━",                   "drew the accent rule (heavy line)"),
    check("upload",              "help text rendered into scrollback"),
    check("38;2;124;58;237",     "violet accent painted before the click"),
    check("38;2;236;72;153",     "pink accent painted after the logo click (theme cycled)"),
    check("\x1b[?1049l",         "restored the primary screen on exit"),
    check("\x1b[?1002l",         "disabled mouse on exit"),
    check("\x1b[?7h",            "re-enabled autowrap on exit"),
]
print("RESULT:", "ALL PASS" if all(results) else "SOME FAILED")
sys.exit(0 if all(results) else 1)
