//! `Editor.readLine`: the raw-mode key loop — the one place a byte from the tty becomes an
//! edit, a motion, a chord the caller handles, or a deferred logo click. Bound as a method
//! on `Editor` by line_edit.zig.
const std = @import("std");
const skin = @import("../app/skin.zig");
const line_edit = @import("line_edit.zig");

const Editor = line_edit.Editor;
const History = line_edit.History;
const Input = line_edit.Input;
const max_line = line_edit.max_line;
const double_click_ms = Editor.double_click_ms;
const words = @import("words.zig");

// An animated secret skin repaints on the idle hook, so the hook runs at its frame rate.
const frame_ms = skin.frame_ms;
// How long an ESC waits for the byte that would make it a sequence (terminals send them together).
const lone_esc_ms = 30;

/// Read one edited line into `buf`: a submitted `.line`, or a chord the caller handles (`.eof`,
/// `.interrupt`, `.copy`, `.paste`). `armed` carries the two-Ctrl-C exit guard across calls.
pub fn readLine(self: *Editor, prompt: []const u8, buf: []u8, hist: *History, completions: []const []const u8, armed: *bool, preset: []const u8) Input {
    var len: usize = @min(preset.len, buf.len);
    if (len != 0) @memcpy(buf[0..len], preset[0..len]); // start with any prefilled text
    var pos: usize = len;
    var hidx: usize = hist.items.items.len; // == items.len means "the fresh line"
    var stash: [max_line]u8 = undefined; // the in-progress line, parked while browsing
    var stash_len: usize = 0;
    // Logo click debounce: a single click is deferred by `double_click_ms` so a second can supersede it
    // as a double-click; `click_pending` fires (cycle the accent) once the window lapses.
    var click_pending = false;
    var click_at: i64 = 0;
    var frame_at: i64 = 0; // when the idle hook last ran
    self.refresh(prompt, buf[0..len], pos);

    while (true) {
        // A burst of wheel notches or drag reports has been read: paint where it left the view.
        if (self.screen) |s| if ((s.scroll_owed or s.drag_owed) and !self.waitReadable(0)) {
            s.settleScroll();
            s.settleDrag();
            self.refresh(prompt, buf[0..len], pos);
        };
        // Input that never pauses (a wheel scroll, a drag) must not freeze an animated skin:
        // between events, the hook runs as soon as a frame is due.
        if (skin.animating() and self.idle_cb != null and self.nowMs() - frame_at >= frame_ms) {
            frame_at = self.nowMs();
            if (self.idle_cb.?(self.idle_ctx.?)) self.refresh(prompt, buf[0..len], pos);
        }
        // Timeout: short while a logo click is pending (so it resolves promptly), else the
        // 500ms idle-hook cadence, else block indefinitely.
        const timeout: i32 = if (click_pending) blk: {
            const rem = double_click_ms - (self.nowMs() - click_at);
            break :blk if (rem <= 0) 1 else @intCast(@min(rem, @as(i64, 500)));
        } else if (self.idle_cb != null) (if (skin.animating()) frame_ms else 500) else -1;
        const ch = blk: {
            switch (self.pollByte(timeout)) {
                .closed => {
                    if (len == 0) {
                        self.abortPending();
                        return .eof;
                    }
                    continue;
                },
                // Only repaint when the hook actually printed something (it clears the line
                // itself first) — otherwise stay silent so the idle prompt never flickers.
                .idle => {
                    // A pending logo click that outlived the double-click window is a single click.
                    if (click_pending and self.nowMs() - click_at >= double_click_ms) {
                        click_pending = false;
                        if (self.logo_cycle_cb) |cb| cb(self.logo_ctx.?);
                        self.refresh(prompt, buf[0..len], pos);
                        continue;
                    }
                    if (self.idle_cb) |cb| {
                        frame_at = self.nowMs();
                        if (cb(self.idle_ctx.?)) self.refresh(prompt, buf[0..len], pos);
                    }
                    continue;
                },
                .byte => |b| break :blk b,
            }
        };
        if (ch != 3) armed.* = false; // any key but Ctrl-C disarms the exit guard
        switch (ch) {
            '\r', '\n' => {
                self.endPromptLine();
                return .{ .line = len };
            },
            3 => { // Ctrl-C: with text selected it COPIES that — what every desktop does,
                // and the one thing the mouse selection was for. Otherwise it confirms exit.
                if (self.copySelection()) {
                    armed.* = false; // a copy is an action of its own: it disarms the exit
                    self.refresh(prompt, buf[0..len], pos);
                    continue;
                }
                self.abortPending();
                self.endPromptLine();
                return .interrupt;
            },
            4 => { // Ctrl-D (EOF): leave the console
                self.abortPending();
                self.endPromptLine();
                return .eof;
            },
            22 => { // Ctrl-V: attach the clipboard's image to the line being typed
                // Plain Ctrl-V, not just the Meta chord: a terminal that keeps Option for itself (VS Code's) never
                // delivers ESC-Ctrl-V, and ⌘V belongs to the terminal — it pastes TEXT and cannot hand us an image.
                if (self.attachClipboard(prompt, buf, &len, &pos)) continue;
                self.endPromptLine();
                return .paste;
            },
            26 => { // Ctrl-Z: take back the last image added
                // Free to bind: ISIG is off in raw mode, so this never suspended us. An
                // image pasted into THIS line goes first; with none, the session's own.
                if (self.dropLastMarker(prompt, buf, &len, &pos)) continue;
                self.endPromptLine();
                return .unpaste;
            },
            21 => { // Ctrl-U: clear the line — and anything pasted into it
                len = 0;
                pos = 0;
                self.abortPending();
                self.refresh(prompt, buf[0..0], 0);
            },
            19 => if (self.screen != null) { // Ctrl-S: copy the selection — or the typed line
                // The prompt row is outside the selectable body, so with nothing highlighted this copies what you
                // are typing: the one piece of text the mouse cannot reach.
                if (!self.copySelection() and len != 0) {
                    if (self.copy_text_cb) |cb| cb(self.logo_ctx.?, buf[0..len]);
                }
                self.refresh(prompt, buf[0..len], pos);
            },
            23 => self.deleteWordBack(prompt, buf, &len, &pos), // Ctrl-W: delete the word before the cursor
            11 => if (pos < len) { // Ctrl-K: kill from the cursor to end of line
                len = pos;
                self.syncPending(buf[0..len]);
                self.refresh(prompt, buf[0..len], pos);
            },
            1 => { // Ctrl-A: home
                pos = 0;
                self.refresh(prompt, buf[0..len], pos);
            },
            5 => { // Ctrl-E: end
                pos = len;
                self.refresh(prompt, buf[0..len], pos);
            },
            9 => self.complete(prompt, buf, &len, &pos, completions), // Tab
            127, 8 => { // Backspace — and, on the byte this tty does NOT send for it,
                // Ctrl-Backspace, which deletes the word (what VS Code, Windows and most
                // Linux terminals send as 0x08).
                if (ch == self.erase or ch == 127) self.backspace(prompt, buf, &len, &pos) else self.deleteWordBack(prompt, buf, &len, &pos);
            },
            27 => { // ESC: a nav/mouse sequence, or an Alt/Meta-modified chord
                // A lone ESC has nothing behind it: wait a moment, never block the idle hook on it.
                const nxt = self.pollNext(lone_esc_ms) orelse continue;
                switch (nxt) {
                    3 => { // Ctrl-Alt-C: copy the image to the clipboard
                        self.abortPending();
                        self.endPromptLine();
                        return .copy;
                    },
                    22 => { // Ctrl-Alt-V: same as Ctrl-V, for terminals that send Meta
                        if (self.attachClipboard(prompt, buf, &len, &pos)) continue;
                        self.endPromptLine();
                        return .paste;
                    },
                    26 => { // Ctrl-Alt-Z: take back the last image added
                        if (self.dropLastMarker(prompt, buf, &len, &pos)) continue;
                        self.endPromptLine();
                        return .unpaste;
                    },
                    // Meta (Alt/Option) word chords — emacs bindings, also Option+←/→.
                    'b' => { // Alt-b: word left
                        pos = words.wordLeft(buf[0..len], pos);
                        self.refresh(prompt, buf[0..len], pos);
                    },
                    'f' => { // Alt-f: word right
                        pos = words.wordRight(buf[0..len], pos);
                        self.refresh(prompt, buf[0..len], pos);
                    },
                    'd' => self.deleteWordFwd(prompt, buf, &len, &pos), // Alt-d: delete word forward
                    8, 127 => self.deleteWordBack(prompt, buf, &len, &pos), // Alt-Backspace: delete word back
                    // Meta-prefixed sequence (ESC ESC [ D = Option+Left under "Option as Meta"):
                    // the leading ESC is the modifier, so parse the inner CSI and force word-motion.
                    27 => {
                        const intro = self.readByte() orelse continue;
                        if (intro == '[' or intro == 'O') {
                            var params: [16]u8 = undefined;
                            const r = self.collectCsi(&params);
                            self.csi(params[0..r.np], r.final, true, prompt, buf, &len, &pos, hist, &hidx, &stash, &stash_len);
                        }
                    },
                    // A terminal's late answer to a query (OSC 17's colour, DA1, a mode report) is
                    // not typing: swallowed whole, never inserted into the line.
                    ']', 'P', '_', '^' => self.drainString(),
                    '[' => {
                        const b2 = self.readByte() orelse continue;
                        if (b2 == '?' or b2 == '>' or b2 == '=') {
                            self.drainCsi();
                        } else if (b2 == '<') { // SGR mouse report
                            self.handleMouse(prompt, buf, &len, &pos, &click_pending, &click_at);
                        } else {
                            // Collect the CSI params so a modified key like Alt-Left
                            // (ESC [ 1 ; 3 D) parses as one sequence, not "1;3D" in the buffer.
                            var params: [16]u8 = undefined;
                            const r = self.collectCsi2(b2, &params);
                            if (r.final == '~' and std.mem.eql(u8, params[0..r.np], "200")) // bracketed paste start
                                self.readPaste(prompt, buf, &len, &pos)
                            else
                                self.csi(params[0..r.np], r.final, false, prompt, buf, &len, &pos, hist, &hidx, &stash, &stash_len);
                        }
                    },
                    'O' => { // application cursor keys: ESC O <final>, no parameters
                        const code = self.readByte() orelse continue;
                        self.csi(&.{}, code, false, prompt, buf, &len, &pos, hist, &hidx, &stash, &stash_len);
                    },
                    else => {}, // other Alt-combo: ignore
                }
            },
            else => if (ch >= 0x20 and len < buf.len) { // printable: insert at cursor
                if (pos < len) std.mem.copyBackwards(u8, buf[pos + 1 .. len + 1], buf[pos..len]);
                buf[pos] = ch;
                pos += 1;
                len += 1;
                self.refresh(prompt, buf[0..len], pos);
            },
        }
    }
}
